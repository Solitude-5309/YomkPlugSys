#pragma once
#include <map>
#include <memory>
#include <mutex>

#include <yomk/server/YomkServerApi.h>

#include "plugin_system/YomkPluginMsg.h"

using namespace yomk;

// YomkPluginManager:插件管理数据层.
// 设计取舍: 全部业务数据在此统一管理——插件表（meta 深拷贝,const char* 指向 so 内存储,
// dlclose 后即悬垂,故必须拷贝）+ 实例表（shared_ptr 唯一所有者）.与 Loader 为独立服务,
// 只通过 request() 调用其接口.
class YomkPluginManager : public YomkService
{
public:
    YomkPluginManager(YomkServer* server);
    ~YomkPluginManager() override = default;
    int init() override;

private:
    struct PluginRecord
    {
        std::string name;
        std::string type;
        std::string version;
        std::string author;
        std::string description;
        std::string path;   // 动态库文件路径
    };

    struct InstanceRecord
    {
        std::shared_ptr<YomkPluginInterface> inst;
        std::string instanceFile;   // 创建时透传的实例文件路径（允许为空,内省用）
    };

    // 请求接口:注释给出 入参消息 -> 响应 data 消息
    yomk::payload::Response load(YomkMsgPtr pkg);              // PluginPath -> String pluginName
    yomk::payload::Response tryUnload(YomkMsgPtr pkg);         // String pluginName -> ok（有存活实例失败）
    yomk::payload::Response forceUnload(YomkMsgPtr pkg);       // String pluginName -> ok（先删全部实例）
    yomk::payload::Response createInstance(YomkMsgPtr pkg);    // CreateReq -> InstanceInfo
    yomk::payload::Response destroyInstance(YomkMsgPtr pkg);   // DestroyReq -> ok
    yomk::payload::Response list(YomkMsgPtr pkg);              // [String pluginName] -> PluginMetaArray
    yomk::payload::Response listInstances(YomkMsgPtr pkg);     // [String pluginName] -> InstanceInfoArray
    yomk::payload::Response pluginByInstance(YomkMsgPtr pkg);  // String 限定名实例名 -> PluginMeta
    yomk::payload::Response instancesFile(YomkMsgPtr pkg);     // String 限定名实例名 -> String 实例文件内容

    // 不变量:限定名 = 插件名.实例名;插件名全局唯一且不含 '.',故限定名全局唯一
    static std::string qualifiedName(const std::string& pluginName, const std::string& instanceName);

    // 内省行格式化（前置条件:调用方已持锁）
    std::string instanceInfoLine(
        const std::string& instanceName, const std::shared_ptr<YomkPluginInterface>& inst) const;

    std::mutex m_mutex;
    std::map<std::string, PluginRecord> m_plugins;   // 插件表:pluginName = meta.name
    // 实例表:按插件分组的二级 map（内层键为裸实例名,插件内唯一）;Manager 是实例生命周期的唯一所有者
    std::map<std::string, std::map<std::string, InstanceRecord>> m_instances;
};
