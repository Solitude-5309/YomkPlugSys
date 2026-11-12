#pragma once
#include <mutex>
#include <string>
#include <vector>

#include <yomk/server/YomkServerApi.h>

#include "YomkPluginMsg.h"

using namespace yomk;

// YomkPluginSystemBuilder:插件系统编排层（四件套之 Builder,唯一用户可感知服务）.
// 设计取舍:
// 1. 只做清单解析与请求编排,不持有插件库也不持有实例:加载/登记/实例生命周期全部归
//    机制层 Loader 与数据层 Manager,本层可随时重建;
// 2. 最近一次构建的清单条目/路径/结果/计数在本层缓存,只服务内省,不参与后续请求决策;
// 3. init 注册内部服务、deinit 逆序注销:注销必须直调注入的 YomkServer*,
//    不得经 yomk::server() 域入口（进程退出期域入口快照会二次释放 server）.

/**
 * @brief Plugin system orchestration service (/YomkPluginSystemBuilder)
 * Parses a .yomk manifest (one entry per line: instanceName@libRelPath@instanceFileRelPath,
 * '#' starts a comment, both paths are relative to the manifest directory) and drives
 * plugin loading plus instance creation purely through requests to the internal services.
 * The service itself never owns plugin libraries or instances.
 * @note This is the only service users register; the mechanism layer (loader) and the
 *       data layer (manager) are registered by init() and are not part of the public API.
 */
// @zh: 插件系统编排服务（/YomkPluginSystemBuilder）。
// @zh: 解析 .yomk 清单（每行一个条目：实例名@动态库相对路径@实例文件相对路径，'#' 起注释，
// @zh: 两段路径均相对清单所在目录），并经请求驱动内部服务完成插件加载与实例创建；
// @zh: 本服务不持有插件库与实例。
// @zh: note 用户只注册本服务；机制层（Loader）与数据层（Manager）由 init() 注册，不属于公开 API。
class YomkPluginSystemBuilder : public YomkService
{
public:
    /**
     * @brief Construct the orchestration service
     * @param server Hosting server injected by the framework; must outlive the service
     */
    // @zh: 构造编排服务。
    // @zh: param server 框架注入的宿主 server，生命周期须覆盖本服务。
    YomkPluginSystemBuilder(YomkServer* server);

    ~YomkPluginSystemBuilder() override = default;

    /**
     * @brief Register internal services and the plugin request handlers
     * @return 0 on success; -1 if an internal service fails to register (handlers are not installed)
     */
    // @zh: 注册内部服务与插件请求处理函数。
    // @zh: return 成功返回 0；内部服务注册失败返回 -1（此时不安装请求处理函数）。
    int init() override;

    /**
     * @brief Unregister internal services in reverse order of init()
     * @warning Unregister through the injected server pointer, never through yomk::server():
     *          during process exit the domain-entry snapshot keeps a reference to the shutting-down
     *          server and double-frees it on return.
     */
    // @zh: 按 init() 的逆序注销内部服务。
    // @zh: warning 注销须直调注入的 server 指针，不得经 yomk::server()：进程退出期域入口快照会
    // @zh:          持有正在 shutdown 的 server，并在其返回时二次释放。
    void deinit() override;

private:
    // 宿主 server 裸指针（构造期由框架传入，生命周期覆盖服务），deinit 注销内部服务用
    YomkServer* m_server = nullptr;

    // 清单条目:实例名@动态库相对路径@实例配置文件相对路径
    struct ManifestEntry
    {
        int lineNo = 0;            // 清单行号（报错定位用）
        std::string instanceName;  // 实例裸名（同一插件内唯一）
        std::string libRelPath;    // 动态库相对清单目录的路径（含平台相关全名）
        std::string instanceFile;  // 实例文件相对清单目录的路径
    };

    // 请求接口:注释给出 入参消息 -> 响应 data 消息
    yomk::payload::Response build(YomkMsgPtr pkg);             // BuildReq -> String 构建汇总
    yomk::payload::Response version(YomkMsgPtr pkg);           // 无 -> String 扩展版本号
    yomk::payload::Response infoBuilder(YomkMsgPtr pkg);       // 无 -> BuilderInfo 最近构建信息
    yomk::payload::Response plugins(YomkMsgPtr pkg);           // 无 -> PluginMetaArray（全量列表）
    yomk::payload::Response plugin(YomkMsgPtr pkg);            // String pluginName -> PluginMeta
    yomk::payload::Response instances(YomkMsgPtr pkg);         // [String pluginName] -> InstanceInfoArray
    yomk::payload::Response pluginByInstance(YomkMsgPtr pkg);  // String 限定名实例名 -> PluginMeta
    yomk::payload::Response instancesFile(YomkMsgPtr pkg);     // String 限定名实例名 -> String 实例文件内容
    yomk::payload::Response unload(YomkMsgPtr pkg);            // String pluginName -> 强制卸载
    yomk::payload::Response tryUnload(YomkMsgPtr pkg);         // String pluginName -> 有存活实例则拒绝

    // 清单解析:首行须为格式标识 #! yomk_plugin_system;# 注释忽略（整行/行内）,
    // 按 @ 切三段校验,路径段拒绝绝对路径
    yomk::payload::Response parseManifest(
        const std::string& workflowPath, std::string& workflowDir, std::vector<ManifestEntry>& entries);

    // 记录最近一次构建状态（内省用）
    void recordBuild(
        const std::string& manifest,
        const std::string& result,
        size_t plugins,
        size_t instances,
        const std::vector<std::string>& entryLines);

    std::mutex m_mutex;
    std::string m_lastManifest;               // 最近一次构建的清单路径
    std::string m_lastResult;                 // 最近一次构建结果："ok" / 失败原因
    int m_lastPlugins = 0;                    // 最近一次构建加载的插件数
    int m_lastInstances = 0;                  // 最近一次构建创建的实例数
    std::vector<std::string> m_lastEntries;   // 成功条目的清单行（实例名@动态库相对路径@实例文件相对路径）
};
