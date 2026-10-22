#include "YomkPluginLoader.h"

#include <dlfcn.h>

#include <algorithm>

YomkPluginLoader::YomkPluginLoader(YomkServer* server) : YomkService(server)
{
    name("/YomkPluginLoader");
}

int YomkPluginLoader::init()
{
    YomkInstallFunc("/load", YomkPluginLoader::load, PluginPath);
    YomkInstallFunc("/unload", YomkPluginLoader::unload, String);
    YomkInstallFunc("/meta", YomkPluginLoader::meta, String);
    YomkInstallFunc("/create", YomkPluginLoader::createInstance, CreateReq);
    YomkInstallFunc("/delete", YomkPluginLoader::deleteInstance, PluginInstance);
    return 0;
}

YomkResponse YomkPluginLoader::load(YomkPkgPtr pkg)
{
    try
    {
        YomkUnPackPkgResponse(pkg, PluginPath, data);
        void* handle = nullptr;
        YomkPluginMetaFunc metaFn = nullptr;
        YomkPluginCreateInstanceFunc createFn = nullptr;
        YomkPluginDeleteInstanceFunc deleteFn = nullptr;
        std::string pluginName;

        handle = dlopen(data->d.path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            return {YomkResponse::eNo, std::string("dlopen failed: ") + dlerror()};
        }
        metaFn = reinterpret_cast<YomkPluginMetaFunc>(dlsym(handle, YOMKPLUGIN_SYMBOL_META));
        createFn = reinterpret_cast<YomkPluginCreateInstanceFunc>(dlsym(handle, YOMKPLUGIN_SYMBOL_CREATE_INSTANCE));
        deleteFn = reinterpret_cast<YomkPluginDeleteInstanceFunc>(dlsym(handle, YOMKPLUGIN_SYMBOL_DELETE_INSTANCE));
        if (!metaFn || !createFn || !deleteFn)
        {
            dlclose(handle);
            return {YomkResponse::eNo, "missing plugin export symbols"};
        }

        const YomkPluginMeta* meta = metaFn();
        if (!meta || !meta->name || !*meta->name)
        {
            dlclose(handle);
            return {YomkResponse::eNo, "invalid plugin meta"};
        }
        if (meta->abi_version != YOMKPLUGIN_ABI_VERSION)
        {
            /* meta 指向插件 so 内静态存储，必须先取值再 dlclose，否则悬垂解引用 */
            const int pluginAbi = meta->abi_version;
            dlclose(handle);
            return {
                YomkResponse::eNo,
                "abi version mismatch: plugin " + std::to_string(pluginAbi) + ", host " +
                    std::to_string(YOMKPLUGIN_ABI_VERSION)};
        }
        pluginName = meta->name;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_plugins.count(pluginName))
        {
            dlclose(handle);
            return {YomkResponse::eNo, "plugin already loaded: " + pluginName};
        }
        LoadedPlugin lib;
        lib.handle = handle;
        lib.metaFn = metaFn;
        lib.createFn = createFn;
        lib.deleteFn = deleteFn;
        m_plugins[pluginName] = lib;
        return {YomkResponse::eOk, "ok", YomkMkPtr(String, pluginName)};
    }
    catch (const std::exception& e)
    {
        return {YomkResponse::eNo, std::string("load exception: ") + e.what()};
    }
    catch (...)
    {
        return {YomkResponse::eNo, "load exception"};
    }
}

YomkResponse YomkPluginLoader::unload(YomkPkgPtr pkg)
{
    try
    {
        YomkUnPackPkgResponse(pkg, String, data);
        const std::string pluginName = data->d;
        void* handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_plugins.find(pluginName);
            if (it == m_plugins.end())
            {
                return {YomkResponse::eNo, "plugin not loaded: " + pluginName};
            }
            purgeDead(pluginName);
            if (aliveCount(pluginName) > 0)
            {
                /* 引用计数保护：仍有存活实例时拒绝卸载 */
                return {YomkResponse::eNo, "instances still alive: " + pluginName};
            }
            handle = it->second.handle;
            m_plugins.erase(it);
            m_alive.erase(pluginName);
            /* dlclose 必须持锁：与 create 持锁的插件工厂调用、meta/info 系列持锁的 metaFn
             * 调用互斥，防止插件代码在 unmap 之后被执行（段错误）。dlclose 毫秒级且卸载
             * 低频，锁内执行可接受 */
            dlclose(handle);
        }
        YOMK_INFO_TAG("YomkPluginLoader", "unload: ", pluginName);
        return {YomkResponse::eOk, "ok", YomkMkPtr(String, "ok")};
    }
    catch (const std::exception& e)
    {
        return {YomkResponse::eNo, std::string("unload exception: ") + e.what()};
    }
    catch (...)
    {
        return {YomkResponse::eNo, "unload exception"};
    }
}

YomkResponse YomkPluginLoader::meta(YomkPkgPtr pkg)
{
    try
    {
        YomkUnPackPkgResponse(pkg, String, data);
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_plugins.find(data->d);
        if (it == m_plugins.end())
        {
            return {YomkResponse::eNo, "plugin not loaded: " + data->d};
        }
        const YomkPluginMeta* meta = it->second.metaFn();
        if (!meta)
        {
            return {YomkResponse::eNo, "invalid plugin meta"};
        }
        PluginMeta out;
        out.abi_version = meta->abi_version;
        out.name = meta->name ? meta->name : "";
        out.type = meta->type ? meta->type : "";
        out.version = meta->version ? meta->version : "";
        out.author = meta->author ? meta->author : "";
        out.description = meta->description ? meta->description : "";
        return {YomkResponse::eOk, "ok", YomkMkPtr(PluginMeta, out)};
    }
    catch (const std::exception& e)
    {
        return {YomkResponse::eNo, std::string("meta exception: ") + e.what()};
    }
    catch (...)
    {
        return {YomkResponse::eNo, "meta exception"};
    }
}

YomkResponse YomkPluginLoader::createInstance(YomkPkgPtr pkg)
{
    try
    {
        YomkUnPackPkgResponse(pkg, CreateReq, req);
        std::shared_ptr<YomkPluginInterface> inst;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_plugins.find(req->d.pluginName);
            if (it == m_plugins.end())
            {
                return {YomkResponse::eNo, "plugin not loaded: " + req->d.pluginName};
            }
            /* 插件工厂必须持锁调用：与 unload 锁内 dlclose 互斥，否则锁外工厂调用
             * 可能落在已 unmap 的插件代码上（段错误）。
             * 契约：createFn 为纯函数、不得回调宿主服务，锁内调用无死锁 */
            YomkPluginInterface* raw = it->second.createFn(req->d.instanceName.c_str(), req->d.instanceFile.c_str());
            if (!raw)
            {
                return {YomkResponse::eNo, "create instance failed: " + req->d.pluginName};
            }
            /* shared_ptr 自定义 deleter 绑定该库的 delete_instance；登记 alive 后再返回 */
            inst.reset(raw, it->second.deleteFn);
            m_alive[req->d.pluginName].push_back(inst);
        }
        return {YomkResponse::eOk, "ok", YomkMkPtr(PluginInstance, inst)};
    }
    catch (const std::exception& e)
    {
        return {YomkResponse::eNo, std::string("create exception: ") + e.what()};
    }
    catch (...)
    {
        return {YomkResponse::eNo, "create exception"};
    }
}

YomkResponse YomkPluginLoader::deleteInstance(YomkPkgPtr pkg)
{
    try
    {
        YomkUnPackPkgResponse(pkg, PluginInstance, data);
        /* shared_ptr 拷贝随响应包销毁时 reset，自动触发 delete_instance */
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& kv : m_alive)
        {
            purgeDead(kv.first);
        }
        return {YomkResponse::eOk, "ok", YomkMkPtr(String, "ok")};
    }
    catch (const std::exception& e)
    {
        return {YomkResponse::eNo, std::string("delete exception: ") + e.what()};
    }
    catch (...)
    {
        return {YomkResponse::eNo, "delete exception"};
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
