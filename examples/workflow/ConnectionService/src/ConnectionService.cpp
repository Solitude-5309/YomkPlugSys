// ConnectionService：workflow 示例插件，演示宿主按清单加载并创建实例、实例文件透传给插件
#include <yomk/plugin_system/YomkPluginApi.h>

#include <string>

class ConnectionServiceInstance : public YomkPluginInterface
{
public:
    ConnectionServiceInstance(const std::string& name, const std::string& instanceFile)
        : m_name(name), m_instanceFile(instanceFile)
    {
    }
    virtual ~ConnectionServiceInstance() {}
    virtual const char* instanceName() const override { return m_name.c_str(); }
    virtual const char* instanceType() const override { return "workflow"; }
private:
    std::string m_name;          // 限定名由宿主指定
    std::string m_instanceFile;  // 不透明透传，插件自行决定是否使用
};

static const YomkPluginMeta g_meta = {
    YOMKPLUGIN_ABI_VERSION,
    "ConnectionService",
    "workflow",
    "1.0.0",
    "Yomk",
    "Workflow example: connection service plugin"};

static const YomkPluginMeta* metaFn()
{
    return &g_meta;
}

static YomkPluginInterface* createFn(const char* instance_name, const char* instance_file)
{
    try
    {
        if (!instance_name || !*instance_name)
        {
            return nullptr;  // 实例名必填：宿主按清单条目传入
        }
        return new ConnectionServiceInstance(instance_name, instance_file ? instance_file : "");
    }
    catch (...)
    {
        return nullptr;
    }
}

static void deleteFn(YomkPluginInterface* instance)
{
    delete instance;
}

YOMKPLUGIN_EXPORT(metaFn, createFn, deleteFn)
