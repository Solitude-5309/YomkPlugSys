#pragma once
#include <map>
#include <memory>
#include <mutex>

#include "YomkPluginMsgs.h"

using namespace yomk;

/*
 * YomkPluginManager：插件管理数据层
 * 全部业务数据在此统一管理：插件表（meta 深拷贝）+ 实例表（shared_ptr 唯一所有者）。
 * 与 Loader 为独立服务，纯通过 YOMK_REQUEST 调用 Loader 接口。
 */
class YomkPluginManager : public YomkService
{
public:
    YomkPluginManager(YomkServer* server);
    ~YomkPluginManager() override = default;
    int init() override;

private:
    /* 插件表记录：meta 深拷贝（const char* 指向 so 内存储，dlclose 后悬垂） */
    struct PluginRecord
    {
        std::string name;
        std::string type;
        std::string version;
        std::string author;
        std::string description;
        /* so 文件路径 */
        std::string path;
    };

    /* 实例表记录：实例指针 + 创建时透传的实例文件路径（内省用） */
    struct InstanceRecord
    {
        std::shared_ptr<YomkPluginInterface> inst;
        /* 创建时透传的实例配置文件绝对路径（可为空：透传参数允许为空） */
        std::string instanceFile;
    };

    /* 请求接口 */
    /* PluginPath -> String pluginName */
    YomkResponse load(YomkPkgPtr pkg);
    /* String pluginName -> ok（有存活实例失败） */
    YomkResponse tryUnload(YomkPkgPtr pkg);
    /* String pluginName -> ok（直接删所有实例） */
    YomkResponse forceUnload(YomkPkgPtr pkg);
    /* CreateReq -> InstanceInfo（instanceName 为限定名 插件名.实例名） */
    YomkResponse createInstance(YomkPkgPtr pkg);
    /* DestroyReq -> ok */
    YomkResponse destroyInstance(YomkPkgPtr pkg);
    /* [String pluginName] -> PluginMetaArray */
    YomkResponse list(YomkPkgPtr pkg);
    /* [String pluginName] -> InstanceInfoArray */
    YomkResponse listInstances(YomkPkgPtr pkg);
    /* 内省接口 */
    /* String 限定名实例名（插件名.实例名）-> PluginMeta（实例所属插件元数据，未知实例拒绝） */
    YomkResponse pluginByInstance(YomkPkgPtr pkg);
    /* String 限定名实例名（插件名.实例名）-> String 实例文件内容（未知实例/文件不可读拒绝） */
    YomkResponse instancesFile(YomkPkgPtr pkg);

    /* 限定名 = 插件名.实例名（插件名全局唯一且不含 '.'，限定名全局唯一） */
    static std::string qualifiedName(const std::string& pluginName, const std::string& instanceName);

    /* 内省行格式化（调用方已持锁） */
    std::string instanceInfoLine(
        const std::string& instanceName, const std::shared_ptr<YomkPluginInterface>& inst) const;

    std::mutex m_mutex;
    /* 插件表：扁平 map，pluginName = meta.name */
    std::map<std::string, PluginRecord> m_plugins;
    /* 实例表：按插件分组的二级 map（内层键为裸实例名，插件内唯一），Manager 是实例生命周期的唯一所有者 */
    std::map<std::string, std::map<std::string, InstanceRecord>> m_instances;
};
