#pragma once
#include <string_view>

// YomkPluginSystem 公开 API 聚合头（四件套之 Api）.
// 设计取舍:
// 1. 域入口 yomk::plugin() 只封装本模块业务终结方法:屏蔽裸 URL,不另造请求通道,
//    url/msg/request 通用链归 yomk::server();调用方也可直连统一通道;带参数的终结方法
//    统一走两段链 manifest(path)/plg_name(name) 绑定 -> 零参终结;
// 2. 分层单向:Interface(ABI 叶子) -> Url(常量叶子) -> Payload -> Msg -> Builder,
//    本文件为聚合入口,无人反向依赖;
// 3. 机制层 src/YomkPluginLoader.h 与数据层 src/YomkPluginManager.h 不随安装导出,
//    仅扩展内部可见;对外可见面即本文件包含的五个头.
#include <yomk/log/YomkLoggerApi.h>
#include <yomk/server/YomkServerApi.h>

#include "YomkPluginInterface.h"
#include "YomkPluginMsg.h"
#include "YomkPluginSystemBuilder.h"
#include "YomkPluginUrl.h"

/* ------------------------- 模块域入口（yomk::plugin()） ------------------------- */

namespace yomk::detail
{
/**
 * @brief Intermediate chain node binding a manifest path, terminated by build()
 * Returned by PluginBuilder::manifest(); like every terminator it hides the raw request
 * channel (yomk::server().url(...).msg(...).request()).
 * @note Holds a copy of the manifest path; use inline in a single chained expression only,
 *       never store or reuse it.
 */
// @zh: 中间链节点：绑定清单路径，链以 build() 终结。
// @zh: 由 PluginBuilder::manifest() 返回；与其它终结方法一样屏蔽裸请求通道。
// @zh: note 持有清单路径的拷贝；仅用于单表达式链式调用，禁止存储或复用。
class PluginManifestBuilder
{
public:
    explicit PluginManifestBuilder(std::string_view workflowPath) : m_workflowPath(workflowPath) {}

    /**
     * @brief Build a plugin project from the bound manifest: load every listed library and create its instances
     * @return Response carrying a String packet with the summary "plugins:N instances:N"
     * @note Blocking; stops at the first failing manifest line, later lines are not processed
     */
    // @zh: 按绑定的清单构建插件工程：加载条目列出的全部动态库并创建实例。
    // @zh: return Response，data 为 String 包，内容为汇总 plugins:N instances:N。
    // @zh: note 阻塞；遇到首个失败条目即停止，后续条目不再处理。
    yomk::payload::Response build() const
    {
        return yomk::server()
            .url(yomk::url::pluginBuild)
            .msg(yomk::msgs::BuildReq::shared(yomk::payload::BuildReq{m_workflowPath}))
            .request();
    }

private:
    std::string m_workflowPath;
};

/**
 * @brief Intermediate chain node binding a plugin name, terminated by unload() or try_unload()
 * Returned by PluginBuilder::plg_name(); both terminators share the bound name and differ only
 * in the unload policy.
 * @note Holds a copy of the plugin name; use inline in a single chained expression only,
 *       never store or reuse it.
 */
// @zh: 中间链节点：绑定插件名，链以 unload() 或 try_unload() 终结。
// @zh: 由 PluginBuilder::plg_name() 返回；两个终结方法共用绑定的名字，仅在卸载策略上不同。
// @zh: note 持有插件名的拷贝；仅用于单表达式链式调用，禁止存储或复用。
class PluginNameBuilder
{
public:
    explicit PluginNameBuilder(std::string_view pluginName) : m_pluginName(pluginName) {}

    /**
     * @brief Force-unload the bound plugin library: clear its instance table first, then close the library
     * @return Response carrying a String packet "ok" on success; failure reason in msg
     * @note References held outside the manager still block the library close (liveness check);
     *       the instance table is already cleared at that point, so the plugin stays loaded,
     *       without instances.
     */
    // @zh: 强制卸载绑定的插件库：先清空该插件的实例表，再卸载动态库（不夺走外部已持有的实例引用）。
    // @zh: return Response，成功时 data 为 String 包 "ok"；失败原因见 msg。
    // @zh: note 外部仍持有实例引用时库不会被关闭（判活拒绝）；此时实例表已清空、插件保持加载但无实例。
    yomk::payload::Response unload() const
    {
        return yomk::server().url(yomk::url::pluginUnload).msg(yomk::msgs::String::shared(m_pluginName)).request();
    }

    /**
     * @brief Try to unload the bound plugin library: refused while any instance is still alive
     * @return Response carrying a String packet "ok" on success; refused if instances remain alive
     */
    // @zh: 尝试卸载绑定的插件库：仍有存活实例时拒绝卸载。
    // @zh: return Response，成功时 data 为 String 包 "ok"；实例存活时返回失败。
    yomk::payload::Response try_unload() const
    {
        return yomk::server().url(yomk::url::pluginTryUnload).msg(yomk::msgs::String::shared(m_pluginName)).request();
    }

private:
    std::string m_pluginName;
};

/**
 * @brief Chainable domain entry of the YomkPluginSystem extension
 * Every terminator is a thin wrapper over the unified request channel
 * (yomk::server().url(...).msg(...).request()), which hides raw URLs.
 * All terminators block until the service replies and return a payload::Response:
 * check status first, then unpack data by the message type documented on the terminator.
 * @note Request packets are built by aggregate-initializing the payload struct,
 *       then wrapping it into the yomk::msgs message class.
 * @warning Holds no state; intended for single-expression chains only, never store or reuse it.
 */
// @zh: YomkPluginSystem 扩展的链式域入口
// @zh: 各终结方法均为统一请求通道（yomk::server().url(...).msg(...).request()）的薄封装，屏蔽裸 URL。
// @zh: 所有终结方法阻塞至服务应答并返回 payload::Response：先判 status，再按终结方法注明的
// @zh: 消息类型解包 data。
// @zh: note 请求包先聚合初始化 payload 结构体，再包入 yomk::msgs 消息类。
// @zh: warning 无状态值类型，仅用于单表达式链式调用，禁止缓存或复用。
class PluginBuilder
{
public:
    /**
     * @brief Bind a manifest file for building, terminating with build()
     * @param workflowPath Path of the .yomk manifest file
     * @return Intermediate chain node carrying the path; call build() on it
     */
    // @zh: 绑定要构建的清单文件，链以 build() 终结。
    // @zh: param workflowPath .yomk 清单文件路径。
    // @zh: return 携带该路径的中间链节点；在其上调用 build()。
    PluginManifestBuilder manifest(std::string_view workflowPath) { return PluginManifestBuilder(workflowPath); }

    /**
     * @brief Bind a plugin name for unloading, terminating with unload() or try_unload()
     * @param pluginName Plugin name (meta name, must not contain '.')
     * @return Intermediate chain node carrying the name; call unload() or try_unload() on it
     */
    // @zh: 绑定要卸载的插件名，链以 unload() 或 try_unload() 终结。
    // @zh: param pluginName 插件名（meta 名，不得含 '.'）。
    // @zh: return 携带该名字的中间链节点；在其上调用 unload() 或 try_unload()。
    PluginNameBuilder plg_name(std::string_view pluginName) { return PluginNameBuilder(pluginName); }

    /**
     * @brief Introspect the latest build performed by this builder
     * @return Response carrying a BuilderInfo packet: manifest entries, path, result, plugin and instance counts
     */
    // @zh: 内省最近一次构建：清单条目、清单路径、结果、插件数与实例数。
    // @zh: return Response，data 为 BuilderInfo 包。
    yomk::payload::Response builder() { return yomk::server().url(yomk::url::pluginBuilderInfo).request(); }

    /**
     * @brief Introspect all loaded plugins
     * @return Response carrying a PluginMetaArray packet
     */
    // @zh: 内省全部已加载插件。
    // @zh: return Response，data 为 PluginMetaArray 包。
    yomk::payload::Response plugins() { return yomk::server().url(yomk::url::pluginListPlugins).request(); }

    /**
     * @brief Introspect loaded plugins filtered by plugin name
     * @param pluginName Plugin name; an unknown name yields a failed response
     * @return Response carrying a PluginMetaArray packet
     */
    // @zh: 内省指定插件（按插件名过滤）。
    // @zh: param pluginName 插件名；未知插件返回失败。
    // @zh: return Response，data 为 PluginMetaArray 包。
    yomk::payload::Response plugins(std::string_view pluginName)
    {
        return yomk::server()
            .url(yomk::url::pluginListPlugins)
            .msg(yomk::msgs::String::shared(std::string(pluginName)))
            .request();
    }

    /**
     * @brief Introspect all created instances
     * @return Response carrying an InstanceInfoArray packet
     */
    // @zh: 内省全部已创建实例。
    // @zh: return Response，data 为 InstanceInfoArray 包。
    yomk::payload::Response infoInstances() { return yomk::server().url(yomk::url::pluginListInstances).request(); }

    /**
     * @brief Introspect instances filtered by plugin name
     * @param pluginName Plugin name; an unknown name yields a failed response
     * @return Response carrying an InstanceInfoArray packet
     */
    // @zh: 内省指定插件下的实例（按插件名过滤）。
    // @zh: param pluginName 插件名；未知插件返回失败。
    // @zh: return Response，data 为 InstanceInfoArray 包。
    yomk::payload::Response infoInstances(std::string_view pluginName)
    {
        return yomk::server()
            .url(yomk::url::pluginListInstances)
            .msg(yomk::msgs::String::shared(std::string(pluginName)))
            .request();
    }

    /**
     * @brief Introspect the metadata of the plugin owning an instance
     * @param instanceName Fully qualified instance name (pluginName.instanceName)
     * @return Response carrying a PluginMeta packet; an unknown instance yields a failed response
     */
    // @zh: 内省实例所属插件的元数据。
    // @zh: param instanceName 限定名实例名（插件名.实例名）。
    // @zh: return Response，data 为 PluginMeta 包；未知实例返回失败。
    yomk::payload::Response infoPluginBy(std::string_view instanceName)
    {
        return yomk::server()
            .url(yomk::url::pluginByInstance)
            .msg(yomk::msgs::String::shared(std::string(instanceName)))
            .request();
    }

    /**
     * @brief Introspect the instance file content passed through when the instance was created
     * @param instanceName Fully qualified instance name (pluginName.instanceName)
     * @return Response carrying a String packet; an unknown instance or unreadable file yields a failed response
     */
    // @zh: 内省实例创建时透传的实例文件内容。
    // @zh: param instanceName 限定名实例名（插件名.实例名）。
    // @zh: return Response，data 为 String 包；未知实例或文件不可读返回失败。
    yomk::payload::Response infoInstancesFile(std::string_view instanceName)
    {
        return yomk::server()
            .url(yomk::url::pluginInstancesFile)
            .msg(yomk::msgs::String::shared(std::string(instanceName)))
            .request();
    }

    /**
     * @brief Query the extension version string
     * @return Response carrying a String packet "YomkPluginSystem vX.Y.Z (WIP)"
     */
    // @zh: 查询扩展版本号。
    // @zh: return Response，data 为 String 包 "YomkPluginSystem vX.Y.Z (WIP)"。
    yomk::payload::Response version() { return yomk::server().url(yomk::url::pluginVersion).request(); }
};
}  // namespace yomk::detail

namespace yomk
{
/**
 * @brief Get the chainable domain entry of the YomkPluginSystem extension
 * @return PluginBuilder by value; use inline in a single chained expression only
 */
// @zh: 获取 YomkPluginSystem 扩展的链式域入口。
// @zh: return 按值返回的 PluginBuilder，仅用于单表达式内联链式调用。
inline detail::PluginBuilder plugin()
{
    return detail::PluginBuilder{};
}
}  // namespace yomk
