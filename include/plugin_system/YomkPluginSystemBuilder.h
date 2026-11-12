#pragma once
#include <mutex>
#include <string>
#include <vector>

#include <yomk/server/YomkServerApi.h>

#include "YomkPluginMsg.h"

using namespace yomk;

/*
 * YomkPluginSystemBuilder：插件系统构建编排层
 * 解析 .yomk 清单（实例名@动态库相对路径@实例配置文件相对路径，# 为注释，
 * 后两段均相对清单所在目录），纯请求调用 Manager 完成插件加载与实例创建
 * （实例配置文件绝对路径作为 instanceFile 透传），自身不持有插件/实例。
 */
class YomkPluginSystemBuilder : public YomkService
{
public:
    YomkPluginSystemBuilder(YomkServer* server);
    ~YomkPluginSystemBuilder() override = default;
    int init() override;
    void deinit() override;

private:
    /* 宿主 server 裸指针（构造期由框架传入，生命周期覆盖服务）：
     * deinit 注销内部服务用——析构流程中不得走 yomk::server() 域入口，
     * 域入口快照会持有正在 shutdown 的 server 并在其返回时二次释放 */
    YomkServer* m_server = nullptr;
    /* 清单条目：实例名@动态库相对路径@实例配置文件相对路径 */
    struct ManifestEntry
    {
        int lineNo = 0;
        /* 实例名（系统唯一主键） */
        std::string instanceName;
        /* 动态库相对清单所在目录的完整路径（含平台相关全名） */
        std::string libRelPath;
        /* 实例配置文件相对清单所在目录的完整路径 */
        std::string instanceFile;
    };

    /* 请求接口 */
    /* BuildReq -> String 构建结果汇总 */
    yomk::payload::Response build(YomkMsgPtr pkg);
    /* 无 -> String 扩展版本号 */
    yomk::payload::Response version(YomkMsgPtr pkg);
    /* 内省接口 */
    /* -> BuilderInfo 最近构建信息（清单条目/路径/结果/插件与实例数） */
    yomk::payload::Response infoBuilder(YomkMsgPtr pkg);
    /* 代理 Manager /list：[String pluginName] -> PluginMetaArray 插件列表 */
    yomk::payload::Response plugins(YomkMsgPtr pkg);
    /* 代理 Manager /list_instances：[String pluginName] -> InstanceInfoArray 实例明细 */
    yomk::payload::Response instances(YomkMsgPtr pkg);
    /* 代理 Manager /plugin_by_instance：String instanceName -> PluginMeta 实例所属插件元数据 */
    yomk::payload::Response pluginByInstance(YomkMsgPtr pkg);
    /* 代理 Manager /instances_file：String instanceName -> String 实例文件内容 */
    yomk::payload::Response instancesFile(YomkMsgPtr pkg);
    /* 代理 Manager /force_unload：String pluginName -> ok 先销毁全部实例再卸载 */
    yomk::payload::Response unload(YomkMsgPtr pkg);
    /* 代理 Manager /try_unload：String pluginName -> ok 有存活实例拒绝 */
    yomk::payload::Response tryUnload(YomkMsgPtr pkg);

    /* 清单解析：首行须为格式标识 #! yomk_plugin_system；# 注释忽略（整行/行内），
     * 按 @ 切分校验三段，路径段拒绝绝对路径 */
    yomk::payload::Response parseManifest(
        const std::string& workflowPath, std::string& workflowDir, std::vector<ManifestEntry>& entries);

    /* 记录最近一次构建状态（内省用） */
    void recordBuild(
        const std::string& manifest,
        const std::string& result,
        size_t plugins,
        size_t instances,
        const std::vector<std::string>& entryLines);

    std::mutex m_mutex;
    /* 最近一次构建的清单路径 */
    std::string m_lastManifest;
    /* 最近一次构建结果（"ok" / 失败原因） */
    std::string m_lastResult;
    /* 最近一次构建加载的插件数 */
    int m_lastPlugins = 0;
    /* 最近一次构建创建的实例数 */
    int m_lastInstances = 0;
    /* 成功条目的清单行（实例名@动态库相对路径@实例文件相对路径） */
    std::vector<std::string> m_lastEntries;
};
