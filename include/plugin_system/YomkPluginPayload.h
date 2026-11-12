#pragma once
#include <string>
#include <vector>

/*
 * YomkPluginSystem 扩展专属载荷（namespace yomk::payload，与 SDK 各
 * Yomk{Module}Payload.h 同一设计）：数据结构体承载业务字段（服务端与用户共用），
 * 对应消息包目录见 YomkPluginMsg.h。
 */
namespace yomk::payload
{
struct PluginPath
{
    /* so 文件路径 */
    std::string path;
};

struct PluginMeta
{
    int abi_version = 0;
    std::string name;
    std::string type;
    std::string version;
    std::string author;
    std::string description;
    /* so 文件路径（Manager 登记时补充） */
    std::string path;
};

struct CreateReq
{
    std::string pluginName;
    /* 调用方指定的实例名（裸名，同一插件内唯一）；Manager 创建时拼接限定名 插件名.实例名 传给插件 */
    std::string instanceName;
    /* 透传参数：允许为空，插件系统不读不解析 */
    std::string instanceFile;
};

struct DestroyReq
{
    std::string pluginName;
    std::string instanceName;
};

struct BuildReq
{
    /* manifest.yomk 清单文件路径 */
    std::string workflowPath;
};

struct InstanceInfo
{
    std::string pluginName;
    /* 全局唯一限定名：插件名.实例名 */
    std::string instanceName;
    std::string instanceType;
};

struct BuilderInfo
{
    /* 最近构建的清单条目行（实例名@动态库相对路径@实例文件相对路径） */
    std::vector<std::string> manifest;
    /* 最近构建的清单路径（空 = 尚未构建） */
    std::string path;
    /* 最近构建结果："ok" 或失败原因（空 = 尚未构建） */
    std::string result;
    /* 最近构建加载的插件数 */
    int plugins = 0;
    /* 最近构建创建的实例数 */
    int instances = 0;
};
}  // namespace yomk::payload

/* 裸名兼容：数据结构体自全局命名空间迁入 yomk::payload，逐个 using 声明引入
 * （不用 using namespace 污染），历史代码以裸名访问业务字段零改动 */
using yomk::payload::BuildReq;
using yomk::payload::BuilderInfo;
using yomk::payload::CreateReq;
using yomk::payload::DestroyReq;
using yomk::payload::InstanceInfo;
using yomk::payload::PluginMeta;
using yomk::payload::PluginPath;
