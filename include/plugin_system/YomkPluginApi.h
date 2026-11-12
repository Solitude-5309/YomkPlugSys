#pragma once
#include <string_view>

/*
 * YomkPluginSystem 模块 API 头（四件套之 Api，与 SDK 各 Yomk{Module}Api.h 同一
 * 链式域入口设计）：插件开发者与宿主用户均只需
 *   #include <yomk/plugin_system/YomkPluginApi.h>
 * 即可获得框架 API、ABI 契约、模块载荷、消息数据类、URL 常量、服务声明、
 * yomk::plugin() 域入口、门面请求宏与 2.x 兼容层。
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
 *   → 本文件聚合入口（域入口 + 门面宏 + 兼容层），无人反向依赖。
 * 内部服务声明头 src/YomkPluginLoader.h（机制层）/ src/YomkPluginManager.h
 * （数据层）不随安装导出，仅编译扩展内部可见。
 */
/* 框架 API：server() 链式请求/服务管理 + payload::Response/ResStatus + yomk::msgs 消息目录 */
#include <yomk/server/YomkServerApi.h>
/* 框架 API：log() 链式日志发射（YOMKPLUGIN_VERSION / 兼容层 YOMK_INFO_TAG 等使用） */
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

/* ------------------------- 用户 API 宏（Builder 唯一用户门面） ------------------------- */

/*
 * 门面宏：Yomk::plugin() 域入口终结方法的同名薄封装（历史契约保持，
 * 表达式形态与链式入口一致，返回 payload::Response）。
 */

/* 按清单构建插件工程（加载所列 so 并创建实例），返回 String(plugins:N instances:N 汇总) */
#define YOMKPLUGIN_BUILD(workflowPath) yomk::plugin().build(workflowPath)

/* 强制卸载插件库：先销毁全部实例再卸载，返回 String(ok/msg) */
#define YOMKPLUGIN_UNLOAD(pluginName) yomk::plugin().unload(pluginName)

/* 尝试卸载插件库：有存活实例则拒绝，返回 String(ok/msg) */
#define YOMKPLUGIN_TRY_UNLOAD(pluginName) yomk::plugin().tryUnload(pluginName)

/* 内省：Builder 最近构建信息（清单条目/路径/结果/插件与实例数），返回 BuilderInfo */
#define YOMKPLUGIN_INFO_BUILDER() yomk::plugin().infoBuilder()

/* 内省：插件列表，返回 PluginMetaArray */
#define YOMKPLUGIN_INFO_PLUGINS() yomk::plugin().infoPlugins()

/* 内省：实例明细列表，返回 InstanceInfoArray */
#define YOMKPLUGIN_INFO_INSTANCES() yomk::plugin().infoInstances()

/* 内省：指定插件下的实例明细列表（未知插件报错），返回 InstanceInfoArray */
#define YOMKPLUGIN_INFO_INSTANCES_BY(pluginName) yomk::plugin().infoInstances(pluginName)

/* 内省：按实例名查所属插件的元数据（未知实例报错），返回 PluginMeta */
#define YOMKPLUGIN_INFO_PLUGIN_BY(instanceName) yomk::plugin().infoPluginBy(instanceName)

/* 内省：按实例名返回其创建时透传的实例文件内容（未知实例报错），返回 String */
#define YOMKPLUGIN_INFO_INSTANCES_FILE(instanceName) yomk::plugin().infoInstancesFile(instanceName)

/* ------------------------- 版本 ------------------------- */

/*
 * 查询扩展版本：宏内部请求 /YomkPluginSystemBuilder/version 并自动解包打印
 * （成功走 yomk::log().info、失败走 yomk::log().error），无返回值。版本值由 CMake
 * 编译期注入扩展 lib（EXTENSION_VERSION，单一来源 project(VERSION)），
 * 返回形如 "YomkPluginSystem v0.2.9 (WIP)" 的版本描述。
 */
#define YOMKPLUGIN_VERSION()                                                              \
    do                                                                                    \
    {                                                                                     \
        auto __resp = yomk::plugin().version();                                           \
        if (__resp.status == yomk::payload::ResStatus::Succeeded)                         \
        {                                                                                 \
            auto __ver = yomk::msgs::unpack<yomk::msgs::String>(__resp.data);             \
            if (__ver)                                                                    \
            {                                                                             \
                yomk::log().tag("YomkPluginSystem").info(__ver->d);                       \
            }                                                                             \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            yomk::log().tag("YomkPluginSystem").error("getVersion failed: ", __resp.msg); \
        }                                                                                 \
    } while (0)

/* ------------------------- YomkServer 2.x 兼容层 ------------------------- */

/*
 * YomkServer 3.x 起框架 API 改为链式域入口（server()/log()/yomk::msgs），2.x 的
 * 全局宏（YOMK_INIT / YOMK_NEW_SERVICE / YOMK_REQUEST / YomkMkPtr / YomkUnPackPkg /
 * YOMK_INFO_TAG 等）不再提供。为让存量宿主代码与本仓库测试/示例平滑迁移，此处以
 * 3.6.18 API 重实现 2.x 宏与类型别名；新代码请直接使用链式 API。
 *
 * 2.x → 3.6.18 对照：
 *   YomkResponse            → yomk::payload::Response（成员 m_status/m_msg/m_data → status/msg/data，
 *                             枚举 eOk/eNo → ResStatus::Succeeded/Failed）
 *   YomkPkgPtr              → YomkMsgPtr
 *   YOMK_INIT()             → yomk::server().init()
 *   YOMK_NEW_SERVICE(T)     → yomk::server().srv<T>().create()
 *   YOMK_DEL_SERVICE(name)  → yomk::server().srv_name(name).remove()
 *   YOMK_REQUEST(url, pkg)  → yomk::server().url(url).msg(pkg).request()
 *                             （服务成员函数内建议直接用基类成员 request(url, pkg)）
 *   YomkMkPtr(T, v)         → yomk::msgs::T::shared(v)
 *   YomkUnPackPkg(pkg, T, v)/YomkUnPackPkgResponse(pkg, T, v)
 *                           → auto v = yomk::msgs::unpack<yomk::msgs::T>(pkg)
 *   YOMK_INFO_TAG(t, ...)/YOMK_ERROR_TAG(t, ...) → yomk::log().tag(t).info(...)/error(...)
 *   yomk::String_           → yomk::msgs::String
 */

/* 类型别名（配合 using namespace yomk 保持裸名可用） */
namespace yomk
{
using YomkResponse = yomk::payload::Response;
using YomkPkgPtr = YomkMsgPtr;
using ResStatus = yomk::payload::ResStatus;
using String_ = yomk::msgs::String;
}  // namespace yomk

/* 生命周期与服务管理（形参名避开链式 API 方法名，防宏参数替换污染） */
#define YOMK_INIT() yomk::server().init()
#define YOMK_NEW_SERVICE(SvcType) yomk::server().srv<SvcType>().create()
#define YOMK_DEL_SERVICE(srvRegName) yomk::server().srv_name(srvRegName).remove()

/* 同步请求（经服务器路由，阻塞至应答） */
#define YOMK_REQUEST(reqUrl, pkg) yomk::server().url(reqUrl).msg(pkg).request()

/* 消息打包 / 解包 */
#define YomkMkPtr(MsgType, value) yomk::msgs::MsgType::shared(value)
#define YomkUnPackPkg(pkg, MsgType, var) auto var = yomk::msgs::unpack<yomk::msgs::MsgType>(pkg)
#define YomkUnPackPkgResponse(pkg, MsgType, var) auto var = yomk::msgs::unpack<yomk::msgs::MsgType>(pkg)

/* 日志发射（tag + 多参数逐个追加，语义同 2.x） */
#define YOMK_INFO_TAG(tagName, ...) yomk::log().tag(tagName).info(__VA_ARGS__)
#define YOMK_ERROR_TAG(tagName, ...) yomk::log().tag(tagName).error(__VA_ARGS__)
