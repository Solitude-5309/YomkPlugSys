#include "YomkManifestParser.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
// 去首尾空白（空格 / 制表 / 换行）
std::string trim(const std::string& s)
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
yomk::payload::Response manifestError(int lineNo, const std::string& what)
{
    return {yomk::payload::ResStatus::Failed, "manifest line " + std::to_string(lineNo) + ": " + what};
}

// 清单解析辅助：截断行内注释（# 起到行尾）
std::string stripComment(const std::string& line)
{
    size_t hashPos = line.find('#');
    return hashPos == std::string::npos ? line : line.substr(0, hashPos);
}

// 清单解析辅助：首行格式标识校验，标识之后只允许空白或行内注释
bool isFormatMarkerLine(const std::string& line)
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
yomk::payload::Response parseBlockHeader(const std::string& line, int lineNo, std::string& name)
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
yomk::payload::Response parseBlockKey(const std::string& line, const int lineNo, ManifestBlock& block)
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

// 解析清单：格式标识 + 实例块（[实例名] + lib/config），输出清单目录与实例块表
yomk::payload::Response parseManifestFile(
    const std::string& manifestPath, std::string& manifestDir, std::vector<yomk::payload::ManifestEntry>& entries)
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
        yomk::payload::ManifestEntry entry;
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
}  // namespace

YomkManifestParser::YomkManifestParser(YomkServer* server) : YomkService(server)
{
    name("/YomkManifestParser");
}

int YomkManifestParser::init()
{
    installFunc("/parse", &YomkManifestParser::parse, yomk::msgs::BuildReq::kName);
    return 0;
}

yomk::payload::Response YomkManifestParser::parse(YomkMsgPtr pkg)
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
        yomk::payload::ManifestParseResult result;
        std::string manifestDir;
        yomk::payload::Response parsed = parseManifestFile(req->d.manifestPath, manifestDir, result.entries);
        if (parsed.status != yomk::payload::ResStatus::Succeeded)
        {
            return parsed;
        }
        result.dir = manifestDir;
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::ManifestParseResult::shared(result)};
    }
    catch (const std::exception& e)
    {
        return {yomk::payload::ResStatus::Failed, std::string("parse exception: ") + e.what()};
    }
    catch (...)
    {
        return {yomk::payload::ResStatus::Failed, "parse exception"};
    }
}
