#include "plug_sys/YomkPlugSysBuilder.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <yomk/log/YomkLoggerApi.h>

#include "YomkPluginLoader.h"
#include "YomkPluginManager.h"

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

// 清单解析辅助：统一错误响应，带清单行号前缀
static yomk::payload::Response manifestError(int lineNo, const std::string& what)
{
    return {yomk::payload::ResStatus::Failed, "manifest line " + std::to_string(lineNo) + ": " + what};
}

// 清单解析辅助：截断行内注释（# 起到行尾）
static std::string stripComment(const std::string& line)
{
    size_t hashPos = line.find('#');
    return hashPos == std::string::npos ? line : line.substr(0, hashPos);
}

// 清单解析辅助：首行格式标识校验，标识之后只允许空白或行内注释
static bool isFormatMarkerLine(const std::string& line)
{
    static const std::string marker = "#! yomk_plug_sys";
    std::string trimmed = trim(line);
    if (trimmed.compare(0, marker.size(), marker) != 0)
    {
        return false;
    }
    std::string rest = trim(trimmed.substr(marker.size()));
    return rest.empty() || rest[0] == '#';
}

// 清单解析辅助：单个实例块的累积状态
struct ManifestBlock
{
    int lineNo = 0;
    std::string name;
    std::string lib;
    std::string cfg;
    bool hasLib = false;
    bool hasCfg = false;

    [[nodiscard]] bool complete() const { return hasLib && hasCfg; }
    [[nodiscard]] std::string missing() const { return hasLib ? "config" : (hasCfg ? "lib" : "lib and config"); }
};

// 清单解析辅助：块头 [实例名]，裸名不得为空、含 '.' 或方括号
static yomk::payload::Response parseBlockHeader(const std::string& line, int lineNo, std::string& name)
{
    if (line.size() < 2 || line.back() != ']')
    {
        return manifestError(lineNo, "malformed instance block header, expect [instanceName]");
    }
    name = trim(line.substr(1, line.size() - 2));
    if (name.empty())
    {
        return manifestError(lineNo, "empty instance name");
    }
    if (name.find('.') != std::string::npos)
    {
        return manifestError(lineNo, "instance name must not contain '.'");
    }
    if (name.find('[') != std::string::npos || name.find(']') != std::string::npos)
    {
        return manifestError(lineNo, "instance name must not contain brackets");
    }
    return {yomk::payload::ResStatus::Succeeded, "ok"};
}

// 清单解析辅助：块内键值行，键仅 lib / config 各一次，值拒绝绝对路径
static yomk::payload::Response parseBlockKey(const std::string& line, const int lineNo, ManifestBlock& block)
{
    size_t eqPos = line.find('=');
    if (eqPos == std::string::npos)
    {
        return manifestError(lineNo, "expect key = value inside instance block");
    }
    std::string key = trim(line.substr(0, eqPos));
    std::string value = trim(line.substr(eqPos + 1));
    if (key != "lib" && key != "config")
    {
        return manifestError(lineNo, "unknown key '" + key + "' (expect lib or config)");
    }
    if (value.empty())
    {
        return manifestError(lineNo, "empty value for '" + key + "'");
    }
    // 格式契约为相对路径：值拒绝绝对路径
    if (value[0] == '/')
    {
        return manifestError(lineNo, key == "lib" ? "lib path must be relative" : "instance config path must be relative");
    }
    bool& seen = (key == "lib") ? block.hasLib : block.hasCfg;
    if (seen)
    {
        return manifestError(lineNo, "duplicate key '" + key + "'");
    }
    seen = true;
    (key == "lib" ? block.lib : block.cfg) = value;
    return {yomk::payload::ResStatus::Succeeded, "ok"};
}

YomkPlugSysBuilder::YomkPlugSysBuilder(YomkServer* server) : YomkService(server), m_server(server)
{
    name("/YomkPlugSysBuilder");
}

int YomkPlugSysBuilder::init()
{
    // 内部服务由本服务接管生命周期：先注册 Loader，再注册 Manager（顺序即依赖次序）
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
    }
}

yomk::payload::Response YomkPlugSysBuilder::parseManifest(
    const std::string& manifestPath, std::string& manifestDir, std::vector<ManifestEntry>& entries)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path manifest = fs::weakly_canonical(manifestPath, ec);
    if (ec || !fs::is_regular_file(manifest))
    {
        return {yomk::payload::ResStatus::Failed, "manifest not found: " + manifestPath};
    }
    std::ifstream in(manifest);
    if (!in)
    {
        return {yomk::payload::ResStatus::Failed, "manifest open failed: " + manifestPath};
    }
    manifestDir = manifest.parent_path().string();

    // 首行格式标识：.yomk 后缀文件按用处格式各异，以标识区分；插件系统清单必须以此开头
    std::string firstLine;
    if (!std::getline(in, firstLine) || !isFormatMarkerLine(firstLine))
    {
        return {yomk::payload::ResStatus::Failed,
                "manifest line 1: format marker not found (expect '#! yomk_plug_sys')"};
    }

    // 首行已消费为格式标识，后续行号从 2 起。清单按实例块组织：
    // [实例名] 起块，块内 key = value 提供 lib / config，块内键严格校验
    ManifestBlock block;
    bool inBlock = false;

    // 块结束（遇下一块头或文件尾）时校验完整性并落入条目表；报错行号取块头行号
    auto flushBlock = [&entries](const ManifestBlock& b) -> yomk::payload::Response
    {
        if (!b.complete())
        {
            return manifestError(b.lineNo, "instance block [" + b.name + "] missing " + b.missing());
        }
        ManifestEntry entry;
        entry.lineNo = b.lineNo;
        entry.instanceName = b.name;
        entry.libRelPath = b.lib;
        entry.instanceCfg = b.cfg;
        entries.push_back(entry);
        return {yomk::payload::ResStatus::Succeeded, "ok"};
    };

    int lineNo = 1;
    std::string line;
    while (std::getline(in, line))
    {
        ++lineNo;
        line = trim(stripComment(line));
        if (line.empty())
        {
            continue;
        }
        // 块内键值行：key = value
        if (line.front() != '[')
        {
            if (!inBlock)
            {
                return manifestError(lineNo, "key outside instance block");
            }
            yomk::payload::Response parsed = parseBlockKey(line, lineNo, block);
            if (parsed.status != yomk::payload::ResStatus::Succeeded)
            {
                return parsed;
            }
            continue;
        }
        // 实例块头：[实例名]；开新块前先校验上一个块的完整性
        if (inBlock)
        {
            yomk::payload::Response flushed = flushBlock(block);
            if (flushed.status != yomk::payload::ResStatus::Succeeded)
            {
                return flushed;
            }
        }
        block = ManifestBlock{};
        yomk::payload::Response header = parseBlockHeader(line, lineNo, block.name);
        if (header.status != yomk::payload::ResStatus::Succeeded)
        {
            return header;
        }
        block.lineNo = lineNo;
        inBlock = true;
    }

    // 文件尾收尾：最后一个实例块同样要校验完整性
    if (inBlock)
    {
        yomk::payload::Response flushed = flushBlock(block);
        if (flushed.status != yomk::payload::ResStatus::Succeeded)
        {
            return flushed;
        }
    }
    return {yomk::payload::ResStatus::Succeeded, "ok"};
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

        std::vector<ManifestEntry> entries;
        std::string manifestDir;
        yomk::payload::Response resp = parseManifest(req->d.manifestPath, manifestDir, entries);
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            recordBuild(req->d.manifestPath, resp.msg, 0, 0, {});
            return resp;
        }

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
        std::vector<std::string> entryLines;
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
                recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                return {yomk::payload::ResStatus::Failed, msg};
            }
            if (!fs::is_regular_file(soPath))
            {
                std::string msg = lineTag;
                msg += ": plugin lib not found: ";
                msg += soPath;
                recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, entryLines);
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
                    recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                    return {yomk::payload::ResStatus::Failed, msg};
                }
                auto idPkg = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
                if (!idPkg)
                {
                    std::string msg = lineTag + ": invalid load response";
                    recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, entryLines);
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
                recordBuild(req->d.manifestPath, msg, loadedPlugins.size(), createdInstances, entryLines);
                return {yomk::payload::ResStatus::Failed, msg};
            }
            ++createdInstances;
            entryLines.push_back("[" + entry.instanceName + "] lib=" + entry.libRelPath + " config=" + entry.instanceCfg);
        }

        std::string summary =
            "plugins:" + std::to_string(loadedPlugins.size()) + " instances:" + std::to_string(createdInstances);
        recordBuild(req->d.manifestPath, "ok", loadedPlugins.size(), createdInstances, entryLines);
        yomk::log().tag("YomkPlugSysBuilder").info("build: ", req->d.manifestPath, " ", summary);
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
    const std::vector<std::string>& entryLines)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastManifest = manifest;
    m_lastResult = result;
    m_lastPlugins = static_cast<int>(plugins);
    m_lastInstances = static_cast<int>(instances);
    m_lastEntries = entryLines;
}
