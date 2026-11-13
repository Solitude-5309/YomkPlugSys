#include "plug_sys/YomkPlugSysBuilder.h"

#include <filesystem>
#include <map>
#include <set>

#include <yomk/log/YomkLoggerApi.h>

#include "YomkManifestParser.h"
#include "YomkPluginLoader.h"
#include "YomkPluginManager.h"

YomkPlugSysBuilder::YomkPlugSysBuilder(YomkServer* server) : YomkService(server), m_server(server)
{
    name("/YomkPlugSysBuilder");
}

int YomkPlugSysBuilder::init()
{
    // 内部服务由本服务接管生命周期：解析器与 Loader 无依赖，Manager 依赖 Loader（注册顺序即依赖次序）
    if (yomk::server().srv<YomkManifestParser>().create() != 0)
    {
        yomk::log().tag("YomkPlugSysBuilder").error("init: register YomkManifestParser failed");
        return -1;
    }
    if (yomk::server().srv<YomkPluginLoader>().create() != 0)
    {
        yomk::log().tag("YomkPlugSysBuilder").error("init: register YomkPluginLoader failed");
        return -1;
    }
    if (yomk::server().srv<YomkPluginManager>().create() != 0)
    {
        yomk::log().tag("YomkPlugSysBuilder").error("init: register YomkPluginManager failed");
        return -1;
    }

    installFunc("/build", &YomkPlugSysBuilder::build, yomk::msgs::BuildReq::kName);
    installFunc("/version", &YomkPlugSysBuilder::version);
    installFunc("/builder", &YomkPlugSysBuilder::infoBuilder);
    installFunc("/plugins", &YomkPlugSysBuilder::plugins);
    installFunc("/plugin", &YomkPlugSysBuilder::plugin, yomk::msgs::String::kName);
    installFunc("/instances", &YomkPlugSysBuilder::instances, yomk::msgs::String::kName);
    installFunc("/plugin_by_instance", &YomkPlugSysBuilder::pluginByInstance, yomk::msgs::String::kName);
    installFunc("/instances_cfg", &YomkPlugSysBuilder::instancesCfg, yomk::msgs::String::kName);
    installFunc("/unload", &YomkPlugSysBuilder::unload, yomk::msgs::String::kName);
    installFunc("/try_unload", &YomkPlugSysBuilder::tryUnload, yomk::msgs::String::kName);
    return 0;
}

void YomkPlugSysBuilder::deinit()
{
    // 逆序注销 init 注册的内部服务。
    // 必须直调注入的 server 指针：进程退出兜底走 shutdown（先清表再锁外逐个 deinit），
    // 此时内部服务已不在表内，delService 返回 -1 忽略即可；若经 yomk::server() 域入口，
    // 其快照会持有正在 shutdown 的 server 并在返回时二次释放（YomkServer 已知缺陷）
    if (m_server)
    {
        m_server->delService("/YomkPluginManager");
        m_server->delService("/YomkPluginLoader");
        m_server->delService("/YomkManifestParser");
    }
}

yomk::payload::Response YomkPlugSysBuilder::build(YomkMsgPtr pkg)
{
    try
    {
        auto req = yomk::msgs::unpack<yomk::msgs::BuildReq>(pkg);
        if (!req)
        {
            return {yomk::payload::ResStatus::Failed,
                    std::string("invalid request msg type: expect ") + yomk::msgs::BuildReq::kName +
                        ", got " + yomk::msgs::typeOf(pkg)};
        }

        // 清单解析委派给内部服务：本层只消费结构化结果（目录 + 实例块表）
        yomk::payload::Response resp = request(
            "/YomkManifestParser/parse", yomk::msgs::BuildReq::shared(yomk::payload::BuildReq{req->d.manifestPath}));
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            recordBuild(req->d.manifestPath, resp.msg, 0, 0, {});
            return resp;
        }
        auto parsed = yomk::msgs::unpack<yomk::msgs::ManifestParseResult>(resp.data);
        if (!parsed)
        {
            recordBuild(req->d.manifestPath, "build: invalid parser response", 0, 0, {});
            return {yomk::payload::ResStatus::Failed, "build: invalid parser response"};
        }
        const std::string manifestDir = parsed->d.dir;
        const std::vector<yomk::payload::ManifestEntry>& entries = parsed->d.entries;

        namespace fs = std::filesystem;
        // 预查插件表：同一 so 已加载时幂等跳过
        std::map<std::string, std::string> pathToPlugin;
        resp = request("/YomkPluginManager/list", nullptr);
        if (resp.status == yomk::payload::ResStatus::Succeeded)
        {
            auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
            if (!arr)
            {
                recordBuild(req->d.manifestPath, "build: invalid plugin list response", 0, 0, {});
                return {yomk::payload::ResStatus::Failed, "build: invalid plugin list response"};
            }
            for (const auto& m : arr->d)
            {
                pathToPlugin[m.path] = m.name;
            }
        }

        std::set<std::string> loadedPlugins;
        std::vector<yomk::payload::InstanceInfo> builtEntries;
        size_t createdInstances = 0;
        for (const auto& entry : entries)
        {
            const std::string lineTag = "manifest line " + std::to_string(entry.lineNo);
            const std::string instancePath = manifestDir + "/" + entry.instanceCfg;
            const std::string soPath = manifestDir + "/" + entry.libRelPath;

            if (!fs::is_regular_file(instancePath))
            {
                std::string msg = lineTag;
                msg += ": instance config not found: ";
                msg += instancePath;
                recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, builtEntries);
                return {yomk::payload::ResStatus::Failed, msg};
            }
            if (!fs::is_regular_file(soPath))
            {
                std::string msg = lineTag;
                msg += ": plugin lib not found: ";
                msg += soPath;
                recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, builtEntries);
                return {yomk::payload::ResStatus::Failed, msg};
            }

            std::string pluginName;
            auto it = pathToPlugin.find(soPath);
            if (it != pathToPlugin.end())
            {
                pluginName = it->second;
            }
            else
            {
                yomk::payload::PluginPath path;
                path.path = soPath;
                resp = request("/YomkPluginManager/load", yomk::msgs::PluginPath::shared(path));
                if (resp.status != yomk::payload::ResStatus::Succeeded)
                {
                    std::string msg = lineTag + ": load failed: " + resp.msg;
                    recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, builtEntries);
                    return {yomk::payload::ResStatus::Failed, msg};
                }
                auto idPkg = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
                if (!idPkg)
                {
                    std::string msg = lineTag + ": invalid load response";
                    recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, builtEntries);
                    return {yomk::payload::ResStatus::Failed, msg};
                }
                pluginName = idPkg->d;
                pathToPlugin[soPath] = pluginName;
            }
            loadedPlugins.insert(pluginName);

            // 实例配置路径作为 instanceCfg 透传，不读取内容
            yomk::payload::CreateReq createReq;
            createReq.pluginName = pluginName;
            createReq.instanceName = entry.instanceName;
            createReq.instanceCfg = instancePath;
            resp = request("/YomkPluginManager/create_instance", yomk::msgs::CreateReq::shared(createReq));
            if (resp.status != yomk::payload::ResStatus::Succeeded)
            {
                std::string msg = lineTag + ": create failed: " + resp.msg;
                recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, builtEntries);
                return {yomk::payload::ResStatus::Failed, msg};
            }
            ++createdInstances;
            yomk::payload::InstanceInfo built;
            built.pluginName = pluginName;
            built.instanceName = pluginName + "." + entry.instanceName;
            built.libFile = soPath;
            built.configFile = instancePath;
            builtEntries.push_back(built);
        }

        std::string summary =
            "plugins:" + std::to_string(loadedPlugins.size()) + " instances:" + std::to_string(createdInstances);
        recordBuild(req->d.manifestPath, "ok", loadedPlugins.size(), createdInstances, builtEntries);
        yomk::log().tag("YomkPlugSysBuilder").info("build: ", req->d.manifestPath, " ", summary);
        yomk::payload::BuilderInfo info;
        info.manifest = builtEntries;
        info.path = req->d.manifestPath;
        info.result = "ok";
        info.plugins = static_cast<int>(loadedPlugins.size());
        info.instances = static_cast<int>(createdInstances);
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::BuilderInfo::shared(info)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("build exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "build exception"};
    }
}

// 扩展版本号：值由 CMake 编译期注入（EXTENSION_VERSION，单一来源 project(VERSION)），
// 经 yomk::plug_sys().version() 对外
yomk::payload::Response YomkPlugSysBuilder::version(YomkMsgPtr pkg)
{
    try
    {
        std::string version = "YomkPlugSys v" EXTENSION_VERSION " (WIP)";
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(version)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("version exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "version exception"};
    }
}

yomk::payload::Response YomkPlugSysBuilder::infoBuilder(YomkMsgPtr pkg)
{
    try
    {
        yomk::payload::BuilderInfo info;
        std::lock_guard<std::mutex> lock(m_mutex);
        info.manifest = m_lastEntries;
        info.path = m_lastManifest;
        info.result = m_lastResult;
        info.plugins = m_lastPlugins;
        info.instances = m_lastInstances;
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::BuilderInfo::shared(info)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("builder exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "builder exception"};
    }
}

// 代理接口：透传入参包给 Manager，参数校验与响应语义归 Manager
yomk::payload::Response YomkPlugSysBuilder::plugins(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/list", nullptr);
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("plugins exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "plugins exception"};
    }
}

// 代理接口：按插件名查询单个插件（与列表接口分离，响应 data 为单条 PluginMeta）
yomk::payload::Response YomkPlugSysBuilder::plugin(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/plugin", pkg);
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("plugin exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "plugin exception"};
    }
}

yomk::payload::Response YomkPlugSysBuilder::instances(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/list_instances", pkg);
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("instances exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "instances exception"};
    }
}

yomk::payload::Response YomkPlugSysBuilder::pluginByInstance(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/plugin_by_instance", pkg);
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

yomk::payload::Response YomkPlugSysBuilder::instancesCfg(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/instances_cfg", pkg);
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("instances_cfg exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "instances_cfg exception"};
    }
}

yomk::payload::Response YomkPlugSysBuilder::unload(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/force_unload", pkg);
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

yomk::payload::Response YomkPlugSysBuilder::tryUnload(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/try_unload", pkg);
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

void YomkPlugSysBuilder::recordBuild(
    const std::string& manifest,
    const std::string& result,
    size_t plugins,
    size_t instances,
    const std::vector<yomk::payload::InstanceInfo>& builtEntries)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastManifest = manifest;
    m_lastResult = result;
    m_lastPlugins = static_cast<int>(plugins);
    m_lastInstances = static_cast<int>(instances);
    m_lastEntries = builtEntries;
}
