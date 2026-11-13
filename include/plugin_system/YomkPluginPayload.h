#pragma once
#include <string>
#include <vector>

// YomkPluginSystem 扩展专属载荷（namespace yomk::payload）.
// 设计取舍: 数据与消息分离——结构体只承载业务字段,同名的消息包类（见 YomkPluginMsg.h）
// 负责跨请求传递;服务端与调用方共用同一结构体定义,字段语义即请求契约.

namespace yomk::payload
{
/**
 * @brief Plugin library load request
 */
// @zh: 插件库加载请求。
struct PluginPath
{
    std::string path;   // 动态库文件路径
};

/**
 * @brief Plugin metadata as registered by the manager
 */
// @zh: Manager 登记后的插件元数据。
struct PluginMeta
{
    int abi_version = 0;   // ABI 契约版本
    std::string name;      // 插件名（全局唯一，不得含 '.'）
    std::string type;      // 插件类型
    std::string version;   // 插件版本
    std::string author;    // 作者
    std::string description;   // 说明
    std::string path;          // 动态库文件路径（Manager 登记时补充）
};

/**
 * @brief Instance creation request
 */
// @zh: 实例创建请求。
struct CreateReq
{
    std::string pluginName;   // 插件名
    // 调用方指定的实例裸名（同一插件内唯一）；Manager 拼接限定名 插件名.实例名 后传给插件
    std::string instanceName;
    std::string instanceCfg;   // 不透明透传参数（实例配置绝对路径）：允许为空，插件系统不读取不解析
};

/**
 * @brief Instance destruction request
 */
// @zh: 实例销毁请求。
struct DestroyReq
{
    std::string pluginName;   // 插件名
    std::string instanceName;   // 实例裸名（同一插件内唯一）
};

/**
 * @brief Manifest build request
 */
// @zh: 清单构建请求。
struct BuildReq
{
    std::string workflowPath;   // manifest.yomk 清单文件路径
};

/**
 * @brief Instance summary used by introspection
 */
// @zh: 内省用实例摘要。
struct InstanceInfo
{
    std::string pluginName;     // 所属插件名
    std::string instanceName;   // 限定名实例名（插件名.实例名）
    std::string instanceType;   // 实例类型
};

/**
 * @brief Latest build state recorded by the builder
 */
// @zh: Builder 记录的最新一次构建状态。
struct BuilderInfo
{
    std::vector<std::string> manifest;   // 最近构建的清单条目行（实例名@动态库相对路径@实例配置相对路径）
    std::string path;                    // 最近构建的清单路径（空 = 尚未构建）
    std::string result;                  // 最近构建结果："ok" 或失败原因（空 = 尚未构建）
    int plugins = 0;                     // 最近构建加载的插件数
    int instances = 0;                   // 最近构建创建的实例数
};
}  // namespace yomk::payload
