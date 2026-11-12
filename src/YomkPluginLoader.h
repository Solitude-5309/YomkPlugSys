#pragma once
#include <map>
#include <mutex>
#include <vector>

#include <yomk/server/YomkServerApi.h>

#include "plugin_system/YomkPluginInterface.h"
#include "plugin_system/YomkPluginMsg.h"

using namespace yomk;

// 宿主 dlsym 契约:符号名与函数类型仅宿主使用,插件侧导出契约见 YomkPluginInterface.h.
#define YOMKPLUGIN_SYMBOL_META "yomk_plugin_meta"
#define YOMKPLUGIN_SYMBOL_CREATE_INSTANCE "yomk_plugin_create_instance"
#define YOMKPLUGIN_SYMBOL_DELETE_INSTANCE "yomk_plugin_delete_instance"

// 插件导出函数类型（dlsym 解析用）
using YomkPluginMetaFunc = const YomkPluginMeta* (*)();
using YomkPluginCreateInstanceFunc = YomkPluginInterface* (*)(const char* instance_name, const char* instance_file);
using YomkPluginDeleteInstanceFunc = void (*)(YomkPluginInterface* instance);

// YomkPluginLoader:插件加载机制层.
// 设计取舍: 纯加载机制,不登记插件、不拥有实例、不管命名.仅持有两种机制状态——
// 句柄表（pluginName -> dlopen 句柄 + 三个导出函数指针,dlclose 的唯一凭据）与
// 实例存活表（pluginName -> weak_ptr 列表,仅判活不延长生命周期）.
class YomkPluginLoader : public YomkService
{
public:
    YomkPluginLoader(YomkServer* server);
    ~YomkPluginLoader() override = default;
    int init() override;

private:
    struct LoadedPlugin
    {
        void* handle = nullptr;
        YomkPluginMetaFunc metaFn = nullptr;
        YomkPluginCreateInstanceFunc createFn = nullptr;
        YomkPluginDeleteInstanceFunc deleteFn = nullptr;
    };

    // 请求接口:注释给出 入参消息 -> 响应 data 消息
    yomk::payload::Response load(YomkMsgPtr pkg);              // PluginPath -> String pluginName
    yomk::payload::Response unload(YomkMsgPtr pkg);            // String pluginName -> ok（有存活实例拒绝）
    yomk::payload::Response meta(YomkMsgPtr pkg);              // String pluginName -> PluginMeta
    yomk::payload::Response createInstance(YomkMsgPtr pkg);    // CreateReq -> PluginInstance
    yomk::payload::Response deleteInstance(YomkMsgPtr pkg);    // PluginInstance -> ok

    // 内部辅助（前置条件:调用方已持锁）
    void purgeDead(const std::string& pluginName);
    int aliveCount(const std::string& pluginName);

    std::mutex m_mutex;
    std::map<std::string, LoadedPlugin> m_plugins;                                 // 句柄表
    std::map<std::string, std::vector<std::weak_ptr<YomkPluginInterface>>> m_alive;  // 实例存活表
};
