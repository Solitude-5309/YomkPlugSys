#include "plugin_system/YomkPluginSystemBuilder.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <yomk/log/YomkLoggerApi.h>

#include "YomkPluginLoader.h"
#include "YomkPluginManager.h"

/* 去除首尾空白 */
static std::string trim(const std::string& s)
{
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

YomkPluginSystemBuilder::YomkPluginSystemBuilder(YomkServer* server) : YomkService(server), m_server(server)
{
    name("/YomkPluginSystemBuilder");
}

int YomkPluginSystemBuilder::init()
{
    /* 内部机制/数据服务由门面接管生命周期：先注册 Loader，再注册 Manager */
    if (yomk::server().srv<YomkPluginLoader>().create() != 0)
    {
        yomk::log().tag("YomkPluginSystemBuilder").error("init: register YomkPluginLoader failed");
        return -1;
    }
    if (yomk::server().srv<YomkPluginManager>().create() != 0)
    {
        yomk::log().tag("YomkPluginSystemBuilder").error("init: register YomkPluginManager failed");
        return -1;
    }

    installFunc("/build", &YomkPluginSystemBuilder::build, yomk::msgs::BuildReq::kName);
    installFunc("/version", &YomkPluginSystemBuilder::version);
    installFunc("/builder", &YomkPluginSystemBuilder::infoBuilder);
    installFunc("/plugins", &YomkPluginSystemBuilder::plugins, yomk::msgs::String::kName);
    installFunc("/instances", &YomkPluginSystemBuilder::instances, yomk::msgs::String::kName);
    installFunc("/plugin_by_instance", &YomkPluginSystemBuilder::pluginByInstance, yomk::msgs::String::kName);
    installFunc("/instances_file", &YomkPluginSystemBuilder::instancesFile, yomk::msgs::String::kName);
    installFunc("/unload", &YomkPluginSystemBuilder::unload, yomk::msgs::String::kName);
    installFunc("/try_unload", &YomkPluginSystemBuilder::tryUnload, yomk::msgs::String::kName);
    return 0;
}

void YomkPluginSystemBuilder::deinit()
{
    /* 逆序注销 init 注册的内部服务。
     * 须直调 server 接口而非 yomk::server() 域入口：进程析构兜底走 shutdown()
     * （先清空服务表再锁外逐个 deinit），此时内部服务已被清出表（delService 返
     * 回 -1 忽略即可）；若经域入口，其快照会持有正在 shutdown 的 server，返回时
     * 触发二次释放导致段错误。显式注销 Builder（服务仍在表）时二者均正常清理 */
    if (m_server)
    {
        m_server->delService("/YomkPluginManager");
        m_server->delService("/YomkPluginLoader");
    }
}

yomk::payload::Response YomkPluginSystemBuilder::parseManifest(
    const std::string& workflowPath, std::string& workflowDir, std::vector<ManifestEntry>& entries)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path manifest = fs::weakly_canonical(workflowPath, ec);
    if (ec || !fs::is_regular_file(manifest))
    {
        return {yomk::payload::ResStatus::Failed, "manifest not found: " + workflowPath};
    }
    std::ifstream in(manifest);
    if (!in)
    {
        return {yomk::payload::ResStatus::Failed, "manifest open failed: " + workflowPath};
    }
    workflowDir = manifest.parent_path().string();

    /* 首行格式标识：.yomk
     * 后缀文件因用处不同格式各异，以标识区分；插件系统清单必须以此开头 */
    static const char* kFormatMarker = "#! yomk_plugin_system";
    std::string firstLine;
    if (!std::getline(in, firstLine) || trim(firstLine) != kFormatMarker)
    {
        return {
            yomk::payload::ResStatus::Failed,
            "manifest line 1: format marker not found (expect '" + std::string(kFormatMarker) + "')"};
    }

    /* 首行已消费为格式标识，条目行号从 2 起 */
    int lineNo = 1;
    std::string line;
    while (std::getline(in, line))
    {
        ++lineNo;
        /* 去除 # 注释：整行注释与行内注释统一截断 */
        size_t hashPos = line.find('#');
        if (hashPos != std::string::npos)
        {
            line.erase(hashPos);
        }
        line = trim(line);
        if (line.empty())
        {
            continue;
        }
        /* 按 @ 切分，必须恰好三段：实例名@动态库相对路径@实例配置文件相对路径
         */
        size_t p1 = line.find('@');
        size_t p2 = (p1 == std::string::npos) ? std::string::npos : line.find('@', p1 + 1);
        size_t p3 = (p2 == std::string::npos) ? std::string::npos : line.find('@', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 != std::string::npos)
        {
            return {
                yomk::payload::ResStatus::Failed,
                "manifest line " + std::to_string(lineNo) + ": expect instanceName@libRelPath@instanceFileRelPath"};
        }
        ManifestEntry entry;
        entry.lineNo = lineNo;
        entry.instanceName = trim(line.substr(0, p1));
        entry.libRelPath = trim(line.substr(p1 + 1, p2 - p1 - 1));
        entry.instanceFile = trim(line.substr(p2 + 1));
        if (entry.instanceName.empty() || entry.libRelPath.empty() || entry.instanceFile.empty())
        {
            return {yomk::payload::ResStatus::Failed, "manifest line " + std::to_string(lineNo) + ": empty segment"};
        }
        /* 格式契约为相对路径：路径段拒绝绝对路径 */
        if (entry.libRelPath[0] == '/')
        {
            return {yomk::payload::ResStatus::Failed, "manifest line " + std::to_string(lineNo) + ": lib path must be relative"};
        }
        if (entry.instanceFile[0] == '/')
        {
            return {
                yomk::payload::ResStatus::Failed, "manifest line " + std::to_string(lineNo) + ": instance file path must be relative"};
        }
        entries.push_back(entry);
    }
    return {yomk::payload::ResStatus::Succeeded, "ok"};
}

yomk::payload::Response YomkPluginSystemBuilder::build(YomkMsgPtr pkg)
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

        std::vector<ManifestEntry> entries;
        std::string workflowDir;
        yomk::payload::Response resp = parseManifest(req->d.workflowPath, workflowDir, entries);
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            recordBuild(req->d.workflowPath, resp.msg, 0, 0, {});
            return resp;
        }

        namespace fs = std::filesystem;
        /* 预查插件表：同一 so 已加载时幂等跳过 */
        std::map<std::string, std::string> pathToPlugin;
        resp = request("/YomkPluginManager/list", nullptr);
        if (resp.status == yomk::payload::ResStatus::Succeeded)
        {
            auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
            if (!arr)
            {
                recordBuild(req->d.workflowPath, "build: invalid plugin list response", 0, 0, {});
                return {yomk::payload::ResStatus::Failed, "build: invalid plugin list response"};
            }
            for (const auto& m : arr->d)
            {
                pathToPlugin[m.path] = m.name;
            }
        }

        std::set<std::string> loadedPlugins;
        std::vector<std::string> entryLines;
        size_t createdInstances = 0;
        for (const auto& entry : entries)
        {
            const std::string lineTag = "manifest line " + std::to_string(entry.lineNo);
            const std::string instancePath = workflowDir + "/" + entry.instanceFile;
            const std::string soPath = workflowDir + "/" + entry.libRelPath;

            if (!fs::is_regular_file(instancePath))
            {
                std::string msg = lineTag;
                msg += ": instance file not found: ";
                msg += instancePath;
                recordBuild(req->d.workflowPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                return {yomk::payload::ResStatus::Failed, msg};
            }
            if (!fs::is_regular_file(soPath))
            {
                std::string msg = lineTag;
                msg += ": plugin lib not found: ";
                msg += soPath;
                recordBuild(req->d.workflowPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                return {yomk::payload::ResStatus::Failed, msg};
            }

            /* 加载插件（已加载则复用，幂等） */
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
                    recordBuild(req->d.workflowPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                    return {yomk::payload::ResStatus::Failed, msg};
                }
                auto idPkg = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
                if (!idPkg)
                {
                    std::string msg = lineTag + ": invalid load response";
                    recordBuild(req->d.workflowPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                    return {yomk::payload::ResStatus::Failed, msg};
                }
                pluginName = idPkg->d;
                pathToPlugin[soPath] = pluginName;
            }
            loadedPlugins.insert(pluginName);

            /* 创建实例：实例文件路径作为 instanceFile 透传，不读取内容 */
            yomk::payload::CreateReq createReq;
            createReq.pluginName = pluginName;
            createReq.instanceName = entry.instanceName;
            createReq.instanceFile = instancePath;
            resp = request("/YomkPluginManager/create_instance", yomk::msgs::CreateReq::shared(createReq));
            if (resp.status != yomk::payload::ResStatus::Succeeded)
            {
                std::string msg = lineTag + ": create failed: " + resp.msg;
                recordBuild(req->d.workflowPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                return {yomk::payload::ResStatus::Failed, msg};
            }
            ++createdInstances;
            entryLines.push_back(entry.instanceName + "@" + entry.libRelPath + "@" + entry.instanceFile);
        }

        std::string summary =
            "plugins:" + std::to_string(loadedPlugins.size()) + " instances:" + std::to_string(createdInstances);
        recordBuild(req->d.workflowPath, "ok", loadedPlugins.size(), createdInstances, entryLines);
        yomk::log().tag("YomkPluginSystemBuilder").info("build: ", req->d.workflowPath, " ", summary);
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(summary)};
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

/* 扩展版本号：值由 CMake 编译期注入（EXTENSION_VERSION，单一来源
 * project(VERSION)）， 经 yomk::plugin().version() 对外 */
yomk::payload::Response YomkPluginSystemBuilder::version(YomkMsgPtr pkg)
{
    try
    {
        std::string version = "YomkPluginSystem v" EXTENSION_VERSION " (WIP)";
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

yomk::payload::Response YomkPluginSystemBuilder::infoBuilder(YomkMsgPtr pkg)
{
    try
    {
        /* BuilderInfo 快照：最近一次构建的清单条目与状态 */
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

/* 代理接口：纯透传入参包给 Manager，参数校验与响应语义归 Manager */
yomk::payload::Response YomkPluginSystemBuilder::plugins(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/list", pkg);
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

yomk::payload::Response YomkPluginSystemBuilder::instances(YomkMsgPtr pkg)
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

yomk::payload::Response YomkPluginSystemBuilder::pluginByInstance(YomkMsgPtr pkg)
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

yomk::payload::Response YomkPluginSystemBuilder::instancesFile(YomkMsgPtr pkg)
{
    try
    {
        return request("/YomkPluginManager/instances_file", pkg);
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

yomk::payload::Response YomkPluginSystemBuilder::unload(YomkMsgPtr pkg)
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

yomk::payload::Response YomkPluginSystemBuilder::tryUnload(YomkMsgPtr pkg)
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

void YomkPluginSystemBuilder::recordBuild(
    const std::string& manifest,
    const std::string& result,
    size_t plugins,
    size_t instances,
    const std::vector<std::string>& entryLines)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastManifest = manifest;
    m_lastResult = result;
    m_lastPlugins = static_cast<int>(plugins);
    m_lastInstances = static_cast<int>(instances);
    m_lastEntries = entryLines;
}
