#include "YomkPluginLoader.h"

#include <dlfcn.h>

#include <yomk/log/YomkLoggerApi.h>

#include <algorithm>

YomkPluginLoader::YomkPluginLoader(YomkServer* server) : YomkService(server)
{
    name("/YomkPluginLoader");
}

int YomkPluginLoader::init()
{
    installFunc("/load", &YomkPluginLoader::load, yomk::msgs::PluginPath::kName);
    installFunc("/unload", &YomkPluginLoader::unload, yomk::msgs::String::kName);
    installFunc("/meta", &YomkPluginLoader::meta, yomk::msgs::String::kName);
    installFunc("/create", &YomkPluginLoader::createInstance, yomk::msgs::CreateReq::kName);
    installFunc("/delete", &YomkPluginLoader::deleteInstance, yomk::msgs::PluginInstance::kName);
    return 0;
}

yomk::payload::Response YomkPluginLoader::load(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::PluginPath>(pkg);
        if (!data)
        {
            return {yomk::payload::ResStatus::Failed,
                    std::string("invalid request msg type: expect ") + yomk::msgs::PluginPath::kName +
                        ", got " + yomk::msgs::typeOf(pkg)};
        }
        void* handle = nullptr;
        YomkPluginMetaFunc metaFn = nullptr;
        YomkPluginCreateInstanceFunc createFn = nullptr;
        YomkPluginDeleteInstanceFunc deleteFn = nullptr;
        std::string pluginName;

        handle = dlopen(data->d.path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            return {yomk::payload::ResStatus::Failed, std::string("dlopen failed: ") + dlerror()};
        }
        metaFn = reinterpret_cast<YomkPluginMetaFunc>(dlsym(handle, YOMKPLUGIN_SYMBOL_META));
        createFn = reinterpret_cast<YomkPluginCreateInstanceFunc>(dlsym(handle, YOMKPLUGIN_SYMBOL_CREATE_INSTANCE));
        deleteFn = reinterpret_cast<YomkPluginDeleteInstanceFunc>(dlsym(handle, YOMKPLUGIN_SYMBOL_DELETE_INSTANCE));
        if (!metaFn || !createFn || !deleteFn)
        {
            dlclose(handle);
            return {yomk::payload::ResStatus::Failed, "missing plugin export symbols"};
        }

        const YomkPluginMeta* meta = metaFn();
        if (!meta || !meta->name || !*meta->name)
        {
            dlclose(handle);
            return {yomk::payload::ResStatus::Failed, "invalid plugin meta"};
        }
        if (meta->abi_version != YOMKPLUGIN_ABI_VERSION)
        {
            // meta 指向插件 so 内静态存储：必须先取值再 dlclose，否则悬垂解引用
            const int pluginAbi = meta->abi_version;
            dlclose(handle);
            return {
                yomk::payload::ResStatus::Failed,
                "abi version mismatch: plugin " + std::to_string(pluginAbi) + ", host " +
                    std::to_string(YOMKPLUGIN_ABI_VERSION)};
        }
        pluginName = meta->name;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_plugins.count(pluginName))
        {
            dlclose(handle);
            return {yomk::payload::ResStatus::Failed, "plugin already loaded: " + pluginName};
        }
        LoadedPlugin lib;
        lib.handle = handle;
        lib.metaFn = metaFn;
        lib.createFn = createFn;
        lib.deleteFn = deleteFn;
        m_plugins[pluginName] = lib;
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(pluginName)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("load exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "load exception"};
    }
}

yomk::payload::Response YomkPluginLoader::unload(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::String>(pkg);
        if (!data)
        {
            return {yomk::payload::ResStatus::Failed,
                    std::string("invalid request msg type: expect ") + yomk::msgs::String::kName +
                        ", got " + yomk::msgs::typeOf(pkg)};
        }
        const std::string pluginName = data->d;
        void* handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_plugins.find(pluginName);
            if (it == m_plugins.end())
            {
                return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + pluginName};
            }
            purgeDead(pluginName);
            // 前置条件：存活表为空，否则拒绝卸载
            if (aliveCount(pluginName) > 0)
            {
                return {yomk::payload::ResStatus::Failed, "instances still alive: " + pluginName};
            }
            handle = it->second.handle;
            m_plugins.erase(it);
            m_alive.erase(pluginName);
            // dlclose 必须持锁：与 create 持锁的工厂调用、meta 持锁的 metaFn 调用互斥，
            // 防止插件代码在 unmap 之后被执行。dlclose 毫秒级且卸载低频，锁内可接受
            dlclose(handle);
        }
        yomk::log().tag("YomkPluginLoader").info("unload: ", pluginName);
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared("ok")};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("unload exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "unload exception"};
    }
}

yomk::payload::Response YomkPluginLoader::meta(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::String>(pkg);
        if (!data)
        {
            return {yomk::payload::ResStatus::Failed,
                    std::string("invalid request msg type: expect ") + yomk::msgs::String::kName +
                        ", got " + yomk::msgs::typeOf(pkg)};
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_plugins.find(data->d);
        if (it == m_plugins.end())
        {
            return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + data->d};
        }
        const YomkPluginMeta* meta = it->second.metaFn();
        if (!meta)
        {
            return {yomk::payload::ResStatus::Failed, "invalid plugin meta"};
        }
        yomk::payload::PluginMeta out;
        out.abi_version = meta->abi_version;
        out.name = meta->name ? meta->name : "";
        out.type = meta->type ? meta->type : "";
        out.version = meta->version ? meta->version : "";
        out.author = meta->author ? meta->author : "";
        out.description = meta->description ? meta->description : "";
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::PluginMeta::shared(out)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("meta exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "meta exception"};
    }
}

yomk::payload::Response YomkPluginLoader::createInstance(YomkMsgPtr pkg)
{
    try
    {
        auto req = yomk::msgs::unpack<yomk::msgs::CreateReq>(pkg);
        if (!req)
        {
            return {yomk::payload::ResStatus::Failed,
                    std::string("invalid request msg type: expect ") + yomk::msgs::CreateReq::kName +
                        ", got " + yomk::msgs::typeOf(pkg)};
        }
        std::shared_ptr<YomkPluginInterface> inst;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_plugins.find(req->d.pluginName);
            if (it == m_plugins.end())
            {
                return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + req->d.pluginName};
            }
            // 工厂必须持锁调用：与 unload 锁内 dlclose 互斥，否则可能落到已 unmap 的代码上。
            // 契约：createFn 为纯函数、不得回调宿主服务，锁内调用无死锁
            YomkPluginInterface* raw = it->second.createFn(req->d.instanceName.c_str(), req->d.instanceCfg.c_str());
            if (!raw)
            {
                return {yomk::payload::ResStatus::Failed, "create instance failed: " + req->d.pluginName};
            }
            // 自定义 deleter 绑定该库的 delete_instance：实例引用归零即销毁；登记存活表后再返回
            inst.reset(raw, it->second.deleteFn);
            m_alive[req->d.pluginName].push_back(inst);
        }
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::PluginInstance::shared(inst)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("create exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "create exception"};
    }
}

yomk::payload::Response YomkPluginLoader::deleteInstance(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::PluginInstance>(pkg);
        if (!data)
        {
            return {yomk::payload::ResStatus::Failed,
                    std::string("invalid request msg type: expect ") + yomk::msgs::PluginInstance::kName +
                        ", got " + yomk::msgs::typeOf(pkg)};
        }
        // 入参拷贝随请求结束释放；实例由 Manager 持有，本接口只回收存活表中的失效项
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& kv : m_alive)
        {
            purgeDead(kv.first);
        }
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared("ok")};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("delete exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "delete exception"};
    }
}

void YomkPluginLoader::purgeDead(const std::string& pluginName)
{
    auto it = m_alive.find(pluginName);
    if (it == m_alive.end())
    {
        return;
    }
    auto& vec = it->second;
    vec.erase(
        std::remove_if(vec.begin(), vec.end(), [](const std::weak_ptr<YomkPluginInterface>& w) { return w.expired(); }),
        vec.end());
}

int YomkPluginLoader::aliveCount(const std::string& pluginName)
{
    auto it = m_alive.find(pluginName);
    return it == m_alive.end() ? 0 : static_cast<int>(it->second.size());
}
