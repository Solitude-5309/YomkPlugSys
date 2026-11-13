#pragma once
#include <mutex>
#include <string>
#include <vector>

#include <yomk/server/YomkServerApi.h>

#include "YomkPlugSysMsg.h"

using namespace yomk;

// YomkPlugSysBuilder:插件系统编排层（四件套之 Builder,唯一用户可感知服务）.
// 设计取舍:
// 1. 只做请求编排与构建状态记录,不解析清单也不持有插件库或实例:清单解析归
//    ManifestParser,加载/登记/实例生命周期归机制层 Loader 与数据层 Manager,本层可随时重建;
// 2. 最近一次构建的实例条目/路径/结果/计数在本层缓存,只服务内省,不参与后续请求决策;
// 3. init 注册内部服务、deinit 逆序注销:注销必须直调注入的 YomkServer*,
//    不得经 yomk::server() 域入口（进程退出期域入口快照会二次释放 server）.

/**
 * @brief Plugin system orchestration service (/YomkPlugSysBuilder)
 * Delegates manifest parsing to /YomkManifestParser, then drives plugin loading plus
 * instance creation purely through requests to the internal services.
 * The service itself never owns plugin libraries or instances.
 * @note This is the only service users register; the manifest parser, mechanism layer
 *       (loader) and data layer (manager) are registered by init() and are not part of
 *       the public API.
 */
// @zh: 插件系统编排服务（/YomkPlugSysBuilder）。
// @zh: 清单解析委派给 /YomkManifestParser，得到实例块表后经请求驱动内部服务完成
// @zh: 插件加载与实例创建；本服务不持有插件库与实例。
// @zh: note 用户只注册本服务；清单解析器、机制层（Loader）与数据层（Manager）
// @zh:       由 init() 注册，不属于公开 API。
class YomkPlugSysBuilder : public YomkService
{
public:
    /**
     * @brief Construct the orchestration service
     * @param server Hosting server injected by the framework; must outlive the service
     */
    // @zh: 构造编排服务。
    // @zh: param server 框架注入的宿主 server，生命周期须覆盖本服务。
    YomkPlugSysBuilder(YomkServer* server);

    ~YomkPlugSysBuilder() override = default;

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

    // 请求接口:注释给出 入参消息 -> 响应 data 消息
    yomk::payload::Response build(YomkMsgPtr pkg);             // BuildReq -> BuilderInfo 构建快照
    yomk::payload::Response version(YomkMsgPtr pkg);           // 无 -> String 扩展版本号
    yomk::payload::Response infoBuilder(YomkMsgPtr pkg);       // 无 -> BuilderInfo 最近构建信息
    yomk::payload::Response plugins(YomkMsgPtr pkg);           // 无 -> PluginMetaArray（全量列表）
    yomk::payload::Response plugin(YomkMsgPtr pkg);            // String pluginName -> PluginMeta
    yomk::payload::Response instances(YomkMsgPtr pkg);         // [String pluginName] -> InstanceInfoArray
    yomk::payload::Response pluginByInstance(YomkMsgPtr pkg);  // String 限定名实例名 -> PluginMeta
    yomk::payload::Response instancesCfg(YomkMsgPtr pkg);      // String 限定名实例名 -> String 实例配置内容
    yomk::payload::Response unload(YomkMsgPtr pkg);            // String pluginName -> 强制卸载
    yomk::payload::Response tryUnload(YomkMsgPtr pkg);         // String pluginName -> 有存活实例则拒绝

    // 记录最近一次构建状态（内省用）
    void recordBuild(
        const std::string& manifest,
        const std::string& result,
        size_t plugins,
        size_t instances,
        const std::vector<yomk::payload::InstanceInfo>& entries);

    std::mutex m_mutex;
    std::string m_lastManifest;               // 最近一次构建的清单路径
    std::string m_lastResult;                 // 最近一次构建结果："ok" / 失败原因
    int m_lastPlugins = 0;                    // 最近一次构建加载的插件数
    int m_lastInstances = 0;                  // 最近一次构建创建的实例数
    std::vector<yomk::payload::InstanceInfo> m_lastEntries;   // 成功创建的实例条目
};
