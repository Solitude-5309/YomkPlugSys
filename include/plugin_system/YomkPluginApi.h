#pragma once
#include <string_view>

/*
 * YomkPluginSystem 模块 API 头（四件套之 Api，与 SDK 各 Yomk{Module}Api.h 同一
 * 链式域入口设计）：插件开发者与宿主用户均只需
 *   #include <yomk/plugin_system/YomkPluginApi.h>
 * 即可获得框架 API、ABI 契约、模块载荷、消息数据类、URL 常量、服务声明与
 * yomk::plugin() 域入口。
 *
 * 设计约定（同 SDK 各域）：
 * 1. Api 头是模块自己的 API，只写本模块业务的终结方法（yomk::plugin()，
 *    屏蔽裸 URL、不另造请求通道），不重复封装通用请求链（url/msg/request 归
 *    yomk::server()）；也可不经域入口直接 yomk::server().url(yomk::url::pluginXxx).request()；
 * 2. 请求一律优先使用统一请求接口 yomk::server().url(<url>).request()，
 *    扩展终结方法只是其上的薄封装。
 *
 * 头文件分层（单向依赖，无 include 环；对外引用统一 <yomk/plugin_system/...>）：
 *   YomkPluginInterface.h（ABI 叶子：插件侧契约——元数据 C 结构 + 实例接口 + 导出契约）
 *   → YomkPluginUrl.h（URL 常量，yomk::url，零依赖叶子）
 *   → YomkPluginPayload.h（扩展专属载荷，yomk::payload）
 *   → YomkPluginMsg.h（扩展专属消息包，yomk::msgs）
 *   → YomkPluginSystemBuilder.h（编排层，唯一用户可感知服务）
 *   → 本文件聚合入口（域入口），无人反向依赖。
 * 内部服务声明头 src/YomkPluginLoader.h（机制层）/ src/YomkPluginManager.h
 * （数据层）不随安装导出，仅编译扩展内部可见。
 */
/* 框架 API：server() 链式请求/服务管理 + payload::Response/ResStatus + yomk::msgs 消息目录 */
#include <yomk/server/YomkServerApi.h>
/* 框架 API：log() 链式日志发射（宿主侧版本打印等场景使用；域入口本体仅依赖 server/msgs） */
#include <yomk/log/YomkLoggerApi.h>

/* ABI 契约：插件侧契约（元数据 C 结构 + 实例接口 + 导出契约） */
#include "YomkPluginInterface.h"
/* 模块 URL 常量（yomk::url 开放命名空间） */
#include "YomkPluginUrl.h"
/* 模块载荷 + 消息数据类（YomkPluginUrl.h 无依赖、YomkPluginMsg.h 依赖 YomkPluginPayload.h） */
#include "YomkPluginMsg.h"
/* 编排层服务声明 */
#include "YomkPluginSystemBuilder.h"

/* ------------------------- 模块域入口（yomk::plugin()） ------------------------- */

/*
 * 扩展模块链式域入口：各终结方法为统一请求接口的薄封装（屏蔽裸 URL），
 * 阻塞至服务应答，返回 payload::Response。消息包类经 YOMK_DEFINE_MSG 生成于
 * yomk::msgs 命名空间，数据结构体在 yomk::payload（业务字段访问用），
 * 构造请求包时先聚合初始化结构体再包入消息类。
 */
namespace yomk::detail
{
class PluginBuilder
{
public:
    /* 按清单构建插件工程（加载所列 so 并创建实例），返回 String(plugins:N instances:N 汇总) */
    yomk::payload::Response build(std::string_view workflowPath)
    {
        return yomk::server()
            .url(yomk::url::pluginBuild)
            .msg(yomk::msgs::BuildReq::shared(yomk::payload::BuildReq{std::string(workflowPath)}))
            .request();
    }

    /* 强制卸载插件库：先销毁全部实例再卸载，返回 String(ok/msg) */
    yomk::payload::Response unload(std::string_view pluginName)
    {
        return yomk::server().url(yomk::url::pluginUnload).msg(yomk::msgs::String::shared(std::string(pluginName))).request();
    }

    /* 尝试卸载插件库：有存活实例则拒绝，返回 String(ok/msg) */
    yomk::payload::Response tryUnload(std::string_view pluginName)
    {
        return yomk::server()
            .url(yomk::url::pluginTryUnload)
            .msg(yomk::msgs::String::shared(std::string(pluginName)))
            .request();
    }

    /* 内省：Builder 最近构建信息（清单条目/路径/结果/插件与实例数），返回 BuilderInfo */
    yomk::payload::Response infoBuilder() { return yomk::server().url(yomk::url::pluginBuilderInfo).request(); }

    /* 内省：插件列表，返回 PluginMetaArray */
    yomk::payload::Response infoPlugins() { return yomk::server().url(yomk::url::pluginListPlugins).request(); }

    /* 内省：指定插件下的插件列表（可选过滤），返回 PluginMetaArray */
    yomk::payload::Response infoPlugins(std::string_view pluginName)
    {
        return yomk::server()
            .url(yomk::url::pluginListPlugins)
            .msg(yomk::msgs::String::shared(std::string(pluginName)))
            .request();
    }

    /* 内省：实例明细列表，返回 InstanceInfoArray */
    yomk::payload::Response infoInstances() { return yomk::server().url(yomk::url::pluginListInstances).request(); }

    /* 内省：指定插件下的实例明细列表（可选过滤），返回 InstanceInfoArray */
    yomk::payload::Response infoInstances(std::string_view pluginName)
    {
        return yomk::server()
            .url(yomk::url::pluginListInstances)
            .msg(yomk::msgs::String::shared(std::string(pluginName)))
            .request();
    }

    /* 内省：按实例名查所属插件的元数据（未知实例报错），返回 PluginMeta */
    yomk::payload::Response infoPluginBy(std::string_view instanceName)
    {
        return yomk::server()
            .url(yomk::url::pluginByInstance)
            .msg(yomk::msgs::String::shared(std::string(instanceName)))
            .request();
    }

    /* 内省：按实例名返回其创建时透传的实例文件内容（未知实例报错），返回 String */
    yomk::payload::Response infoInstancesFile(std::string_view instanceName)
    {
        return yomk::server()
            .url(yomk::url::pluginInstancesFile)
            .msg(yomk::msgs::String::shared(std::string(instanceName)))
            .request();
    }

    /* 版本查询，返回 String("YomkPluginSystem vX.Y.Z (WIP)") */
    yomk::payload::Response version() { return yomk::server().url(yomk::url::pluginVersion).request(); }
};
}  // namespace yomk::detail

namespace yomk
{
/**
 * @brief YomkPluginSystem 扩展域链式入口
 * @return PluginBuilder（按值, 仅用于单表达式链式调用）
 */
inline detail::PluginBuilder plugin()
{
    return detail::PluginBuilder{};
}
}  // namespace yomk
