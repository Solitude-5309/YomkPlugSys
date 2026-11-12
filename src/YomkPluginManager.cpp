#include "YomkPluginManager.h"

#include <fstream>
#include <iterator>

#include <yomk/log/YomkLoggerApi.h>

YomkPluginManager::YomkPluginManager(YomkServer* server) : YomkService(server)
{
    name("/YomkPluginManager");
}

int YomkPluginManager::init()
{
    installFunc("/load", &YomkPluginManager::load, yomk::msgs::PluginPath::kName);
    installFunc("/try_unload", &YomkPluginManager::tryUnload, yomk::msgs::String::kName);
    installFunc("/force_unload", &YomkPluginManager::forceUnload, yomk::msgs::String::kName);
    installFunc("/create_instance", &YomkPluginManager::createInstance, yomk::msgs::CreateReq::kName);
    installFunc("/destroy_instance", &YomkPluginManager::destroyInstance, yomk::msgs::DestroyReq::kName);
    installFunc("/list", &YomkPluginManager::list, yomk::msgs::String::kName);
    installFunc("/list_instances", &YomkPluginManager::listInstances, yomk::msgs::String::kName);
    installFunc("/plugin_by_instance", &YomkPluginManager::pluginByInstance, yomk::msgs::String::kName);
    installFunc("/instances_file", &YomkPluginManager::instancesFile, yomk::msgs::String::kName);
    return 0;
}

yomk::payload::Response YomkPluginManager::load(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::PluginPath>(pkg);
        if (!data)
        {
            return invalidReqMsg(pkg, yomk::msgs::PluginPath::kName);
        }

        /* 锁外请求 Loader：dlopen + 符号解析 + abi/重名校验 + 登记句柄 */
        yomk::payload::Response resp = request("/YomkPluginLoader/load", yomk::msgs::PluginPath::shared(data->d));
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            return resp;
        }
        auto pluginNamePkg = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
        if (!pluginNamePkg)
        {
            return {yomk::payload::ResStatus::Failed, "load: invalid loader response"};
        }
        const std::string pluginName = pluginNamePkg->d;

        /* 锁外请求 meta（Loader 不缓存，实时调 meta 导出函数） */
        resp = request("/YomkPluginLoader/meta", yomk::msgs::String::shared(pluginName));
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            request("/YomkPluginLoader/unload", yomk::msgs::String::shared(pluginName));
            return resp;
        }
        auto metaPkg = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
        if (!metaPkg)
        {
            return {yomk::payload::ResStatus::Failed, "load: invalid meta response"};
        }
        if (metaPkg->d.name.find('.') != std::string::npos)
        {
            /* 限定名 = 插件名.实例名，插件名含 '.' 会破坏限定名全局唯一性 */
            request("/YomkPluginLoader/unload", yomk::msgs::String::shared(pluginName));
            return {yomk::payload::ResStatus::Failed, "plugin name must not contain '.'"};
        }

        PluginRecord rec;
        rec.name = metaPkg->d.name;
        rec.type = metaPkg->d.type;
        rec.version = metaPkg->d.version;
        rec.author = metaPkg->d.author;
        rec.description = metaPkg->d.description;
        rec.path = data->d.path;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_plugins.count(pluginName))
        {
            /* 并发竞争兜底：正常路径 Loader 已拒绝重名 */
            return {yomk::payload::ResStatus::Failed, "plugin already loaded: " + pluginName};
        }
        m_plugins[pluginName] = rec;
        /* 保证实例组存在，不变量：实例表外层 pluginName 必在插件表中 */
        m_instances[pluginName];
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

yomk::payload::Response YomkPluginManager::tryUnload(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::String>(pkg);
        if (!data)
        {
            return invalidReqMsg(pkg, yomk::msgs::String::kName);
        }
        const std::string pluginName = data->d;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_plugins.count(pluginName))
            {
                return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + pluginName};
            }
        }
        /* 尝试卸载：委托 Loader /unload 以 weak 存活表做全局判活（覆盖 Manager 之外的持有者） */
        yomk::payload::Response resp = request("/YomkPluginLoader/unload", yomk::msgs::String::shared(pluginName));
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            return resp;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_instances.erase(pluginName);
        m_plugins.erase(pluginName);
        yomk::log().tag("YomkPluginManager").info("try_unload: ", pluginName, " ok");
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared("ok")};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("try_unload exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "try_unload exception"};
    }
}

yomk::payload::Response YomkPluginManager::forceUnload(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::String>(pkg);
        if (!data)
        {
            return invalidReqMsg(pkg, yomk::msgs::String::kName);
        }
        const std::string pluginName = data->d;
        std::map<std::string, InstanceRecord> group;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_plugins.count(pluginName))
            {
                return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + pluginName};
            }
            auto it = m_instances.find(pluginName);
            if (it != m_instances.end())
            {
                group = std::move(it->second);
                m_instances.erase(it);
            }
        }
        /* 锁外析构全部实例（触发 delete_instance，代码在 so 中，必须先于 dlclose） */
        group.clear();

        yomk::payload::Response resp = request("/YomkPluginLoader/unload", yomk::msgs::String::shared(pluginName));
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            return resp;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_plugins.erase(pluginName);
        yomk::log().tag("YomkPluginManager").info("force_unload: ", pluginName, " ok");
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared("ok")};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("force_unload exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "force_unload exception"};
    }
}

yomk::payload::Response YomkPluginManager::createInstance(YomkMsgPtr pkg)
{
    try
    {
        auto req = yomk::msgs::unpack<yomk::msgs::CreateReq>(pkg);
        if (!req)
        {
            return invalidReqMsg(pkg, yomk::msgs::CreateReq::kName);
        }
        if (req->d.instanceName.empty())
        {
            return {yomk::payload::ResStatus::Failed, "instance name required"};
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_plugins.count(req->d.pluginName))
            {
                return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + req->d.pluginName};
            }
            auto it = m_instances.find(req->d.pluginName);
            if (it != m_instances.end() && it->second.count(req->d.instanceName))
            {
                return {yomk::payload::ResStatus::Failed, "duplicate instance name: " + req->d.instanceName};
            }
        }

        /* 锁外请求 Loader 创建实例：传限定名（插件名.实例名，全局唯一），插件以此作为实例身份 */
        yomk::payload::CreateReq loaderReq = req->d;
        loaderReq.instanceName = qualifiedName(req->d.pluginName, req->d.instanceName);
        yomk::payload::Response resp = request("/YomkPluginLoader/create", yomk::msgs::CreateReq::shared(loaderReq));
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            return resp;
        }
        auto instPkg = yomk::msgs::unpack<yomk::msgs::PluginInstance>(resp.data);
        if (!instPkg)
        {
            return {yomk::payload::ResStatus::Failed, "create_instance: invalid loader response"};
        }
        std::shared_ptr<YomkPluginInterface> inst = instPkg->d;
        if (!inst)
        {
            return {yomk::payload::ResStatus::Failed, "empty plugin instance"};
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_plugins.count(req->d.pluginName))
        {
            /* 创建期间插件被卸载，inst 随响应销毁 */
            return {yomk::payload::ResStatus::Failed, "plugin unloaded during create: " + req->d.pluginName};
        }
        if (!m_instances[req->d.pluginName].emplace(req->d.instanceName, InstanceRecord{inst, req->d.instanceFile}).second)
        {
            /* 并发兜底：创建期间同名实例已被登记 */
            return {yomk::payload::ResStatus::Failed, "duplicate instance name: " + req->d.instanceName};
        }
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(req->d.instanceName)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("create_instance exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "create_instance exception"};
    }
}

yomk::payload::Response YomkPluginManager::destroyInstance(YomkMsgPtr pkg)
{
    try
    {
        auto req = yomk::msgs::unpack<yomk::msgs::DestroyReq>(pkg);
        if (!req)
        {
            return invalidReqMsg(pkg, yomk::msgs::DestroyReq::kName);
        }
        std::shared_ptr<YomkPluginInterface> inst;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_instances.find(req->d.pluginName);
            if (it != m_instances.end())
            {
                auto jt = it->second.find(req->d.instanceName);
                if (jt != it->second.end())
                {
                    inst = std::move(jt->second.inst);
                    it->second.erase(jt);
                }
            }
        }
        if (!inst)
        {
            return {yomk::payload::ResStatus::Failed, "instance not found: " + req->d.pluginName + "/" + req->d.instanceName};
        }
        /* 锁外 reset，自动触发 delete_instance */
        inst.reset();
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared("ok")};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("destroy_instance exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "destroy_instance exception"};
    }
}

yomk::payload::Response YomkPluginManager::list(YomkMsgPtr pkg)
{
    try
    {
        /* 可选入参：指定 pluginName 只返回单个插件 */
        std::string filter;
        if (pkg)
        {
            auto s = yomk::msgs::unpack<yomk::msgs::String>(pkg);
            if (s)
            {
                filter = s->d;
            }
        }

        std::vector<yomk::payload::PluginMeta> metas;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& kv : m_plugins)
        {
            if (!filter.empty() && kv.first != filter)
            {
                continue;
            }
            yomk::payload::PluginMeta m;
            m.abi_version = YOMKPLUGIN_ABI_VERSION;
            m.name = kv.second.name;
            m.type = kv.second.type;
            m.version = kv.second.version;
            m.author = kv.second.author;
            m.description = kv.second.description;
            m.path = kv.second.path;
            metas.push_back(m);
        }
        if (!filter.empty() && metas.empty())
        {
            return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + filter};
        }
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::PluginMetaArray::shared(metas)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("list exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "list exception"};
    }
}

/* 按实例名反查所属插件元数据：遍历实例表二级 map 定位插件，再取插件表记录 */
yomk::payload::Response YomkPluginManager::pluginByInstance(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::String>(pkg);
        if (!data)
        {
            return invalidReqMsg(pkg, yomk::msgs::String::kName);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        /* 输入为限定名（插件名.实例名）：按插件组前缀匹配 + 组内裸名查找，不做字符串反解析 */
        std::string pluginName;
        for (const auto& kv : m_instances)
        {
            const std::string prefix = kv.first + ".";
            if (data->d.rfind(prefix, 0) != 0)
            {
                continue;
            }
            if (kv.second.count(data->d.substr(prefix.size())) != 0)
            {
                pluginName = kv.first;
                break;
            }
        }
        if (pluginName.empty())
        {
            return {yomk::payload::ResStatus::Failed, "instance not found: " + data->d};
        }
        auto it = m_plugins.find(pluginName);
        if (it == m_plugins.end())
        {
            return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + pluginName};
        }
        yomk::payload::PluginMeta m;
        m.abi_version = YOMKPLUGIN_ABI_VERSION;
        m.name = it->second.name;
        m.type = it->second.type;
        m.version = it->second.version;
        m.author = it->second.author;
        m.description = it->second.description;
        m.path = it->second.path;
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::PluginMeta::shared(m)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("plugin_by_instance exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "plugin_by_instance exception"};
    }
}

/* 按实例名返回创建时透传的实例文件内容（未知实例/文件不可读拒绝） */
yomk::payload::Response YomkPluginManager::instancesFile(YomkMsgPtr pkg)
{
    try
    {
        auto data = yomk::msgs::unpack<yomk::msgs::String>(pkg);
        if (!data)
        {
            return invalidReqMsg(pkg, yomk::msgs::String::kName);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        /* 输入为限定名（插件名.实例名）：按插件组前缀匹配 + 组内裸名查找 */
        bool found = false;
        std::string instanceFile;
        for (const auto& kv : m_instances)
        {
            const std::string prefix = kv.first + ".";
            if (data->d.rfind(prefix, 0) != 0)
            {
                continue;
            }
            auto it = kv.second.find(data->d.substr(prefix.size()));
            if (it != kv.second.end())
            {
                found = true;
                instanceFile = it->second.instanceFile;
                break;
            }
        }
        if (!found)
        {
            return {yomk::payload::ResStatus::Failed, "instance not found: " + data->d};
        }
        if (instanceFile.empty())
        {
            return {yomk::payload::ResStatus::Failed, "instance file not recorded: " + data->d};
        }
        std::ifstream in(instanceFile);
        if (!in)
        {
            return {yomk::payload::ResStatus::Failed, "instance file not readable: " + instanceFile};
        }
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(content)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("instances_file exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "instances_file exception"};
    }
}

yomk::payload::Response YomkPluginManager::listInstances(YomkMsgPtr pkg)
{
    try
    {
        /* 可选入参：指定 pluginName 只返回该插件的实例 */
        std::string filter;
        if (pkg)
        {
            auto s = yomk::msgs::unpack<yomk::msgs::String>(pkg);
            if (s)
            {
                filter = s->d;
            }
        }

        std::vector<yomk::payload::InstanceInfo> infos;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!filter.empty())
        {
            if (!m_plugins.count(filter))
            {
                return {yomk::payload::ResStatus::Failed, "plugin not loaded: " + filter};
            }
            auto it = m_instances.find(filter);
            if (it != m_instances.end())
            {
                for (const auto& instKv : it->second)
                {
                    yomk::payload::InstanceInfo info;
                    info.pluginName = filter;
                    info.instanceName = qualifiedName(filter, instKv.first);
                    info.instanceType = instKv.second.inst->instanceType() ? instKv.second.inst->instanceType() : "";
                    infos.push_back(info);
                }
            }
        }
        else
        {
            for (const auto& kv : m_instances)
            {
                for (const auto& instKv : kv.second)
                {
                    yomk::payload::InstanceInfo info;
                    info.pluginName = kv.first;
                    info.instanceName = qualifiedName(kv.first, instKv.first);
                    info.instanceType = instKv.second.inst->instanceType() ? instKv.second.inst->instanceType() : "";
                    infos.push_back(info);
                }
            }
        }
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::InstanceInfoArray::shared(infos)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("list_instances exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "list_instances exception"};
    }
}

std::string YomkPluginManager::instanceInfoLine(
    const std::string& instanceName, const std::shared_ptr<YomkPluginInterface>& inst) const
{
    std::string type = inst->instanceType() ? inst->instanceType() : "";
    return instanceName + " [" + type + "] userData:" + (inst->userData ? "on" : "off");
}

std::string YomkPluginManager::qualifiedName(const std::string& pluginName, const std::string& instanceName)
{
    return pluginName + "." + instanceName;
}
