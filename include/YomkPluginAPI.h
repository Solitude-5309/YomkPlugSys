#pragma once

/*
 * YomkPluginSystem 统一 API 入口：插件开发者与宿主用户均只需
 *   #include <YomkPluginSystem/YomkPluginAPI.h>
 * 即可获得框架 API、ABI 契约、消息数据类、服务声明与门面请求宏。
 *
 * 头文件分层（单向依赖，无 include 环）：
 *   YomkPluginInterface.h（ABI 叶子：插件侧契约——元数据 C 结构 + 实例接口 + 导出契约）
 *   → YomkPluginMsgs.h（消息数据类）
 *   → YomkPluginSystemBuilder.h（编排层，唯一用户可感知服务）
 *   → 本文件仅作聚合入口，无人反向依赖，内部 include 顺序不影响正确性。
 * 内部服务声明头 src/YomkPluginLoader.h（机制层）/ src/YomkPluginManager.h
 * （数据层）不随安装导出，仅编译扩展内部可见。
 */
/* YOMK_INIT / YOMK_NEW_SERVICE / YOMK_REQUEST / YomkMkPtr */
#include <YomkServer/YomkAPI.h>

/* ABI 契约：插件侧契约（元数据 C 结构 + 实例接口 + 导出契约） */
#include "YomkPluginInterface.h"

/* ------------------------- 消息数据类与服务声明 ------------------------- */

/* 消息数据类：结构体 + YomkMsg 注册 */
#include "YomkPluginMsgs.h"
/* 编排层服务声明 */
#include "YomkPluginSystemBuilder.h"

/* ------------------------- 用户 API 宏（Builder 唯一用户门面） ------------------------- */

/* 按清单构建插件工程（加载所列 so 并创建实例），返回 String(plugins:N instances:N 汇总) */
#define YOMKPLUGIN_BUILD(workflowPath) \
    YOMK_REQUEST("/YomkPluginSystemBuilder/build", YomkMkPtr(BuildReq, BuildReq{workflowPath}))

/* 强制卸载插件库：先销毁全部实例再卸载，返回 String(ok/msg) */
#define YOMKPLUGIN_UNLOAD(pluginName) YOMK_REQUEST("/YomkPluginSystemBuilder/unload", YomkMkPtr(String, pluginName))

/* 尝试卸载插件库：有存活实例则拒绝，返回 String(ok/msg) */
#define YOMKPLUGIN_TRY_UNLOAD(pluginName) YOMK_REQUEST("/YomkPluginSystemBuilder/try_unload", YomkMkPtr(String, pluginName))

/* 内省：Builder 最近构建信息（清单条目/路径/结果/插件与实例数），返回 BuilderInfo */
#define YOMKPLUGIN_INFO_BUILDER() YOMK_REQUEST("/YomkPluginSystemBuilder/builder", nullptr)

/* 内省：插件列表，返回 PluginMetaArray */
#define YOMKPLUGIN_INFO_PLUGINS() YOMK_REQUEST("/YomkPluginSystemBuilder/plugins", nullptr)

/* 内省：实例明细列表，返回 InstanceInfoArray */
#define YOMKPLUGIN_INFO_INSTANCES() YOMK_REQUEST("/YomkPluginSystemBuilder/instances", nullptr)

/* 内省：指定插件下的实例明细列表（未知插件报错），返回 InstanceInfoArray */
#define YOMKPLUGIN_INFO_INSTANCES_BY(pluginName) \
    YOMK_REQUEST("/YomkPluginSystemBuilder/instances", YomkMkPtr(String, pluginName))

/* 内省：按实例名查所属插件的元数据（未知实例报错），返回 PluginMeta */
#define YOMKPLUGIN_INFO_PLUGIN_BY(instanceName) \
    YOMK_REQUEST("/YomkPluginSystemBuilder/plugin_by_instance", YomkMkPtr(String, instanceName))

/* 内省：按实例名返回其创建时透传的实例文件内容（未知实例报错），返回 String */
#define YOMKPLUGIN_INFO_INSTANCES_FILE(instanceName) \
    YOMK_REQUEST("/YomkPluginSystemBuilder/instances_file", YomkMkPtr(String, instanceName))

/* ------------------------- 版本 ------------------------- */

/*
 * 查询扩展版本：宏内部请求 /YomkPluginSystemBuilder/version 并自动解包打印
 * （成功走 YOMK_INFO_TAG、失败走 YOMK_ERROR_TAG），无返回值。版本值由 CMake
 * 编译期注入扩展 lib（EXTENSION_VERSION，单一来源 project(VERSION)），
 * 返回形如 "YomkPluginSystem v0.0.16 (WIP)" 的版本描述。
 */
#define YOMKPLUGIN_VERSION()                                                         \
    do                                                                               \
    {                                                                                \
        auto __resp = YOMK_REQUEST("/YomkPluginSystemBuilder/version", nullptr);     \
        if (__resp.m_status == YomkResponse::eOk)                                    \
        {                                                                            \
            YomkUnPackPkg(__resp.m_data, String, __ver);                             \
            if (__ver)                                                               \
            {                                                                        \
                YOMK_INFO_TAG("YomkPluginSystem", __ver->d);                         \
            }                                                                        \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            YOMK_ERROR_TAG("YomkPluginSystem", "getVersion failed: ", __resp.m_msg); \
        }                                                                            \
    } while (0)
