// TestPlugin：插件系统示例插件
// 演示完整 ABI 契约：meta 常量导出 + 实例工厂 + 实例删除。
// instance_name 由宿主创建实例时指定（限定名 插件名.实例名，全局唯一），
// instance_cfg 独立透传，两者互不影响。
#include <yomk/plug_sys/YomkPlugSysApi.h>

#include <string>

class MyInstance : public YomkPluginInterface
{
public:
    MyInstance(const std::string& name, const std::string& instanceCfg) : m_name(name), m_instanceCfg(instanceCfg) {}
    virtual ~MyInstance() {}

    virtual const char* instanceName() const override { return m_name.c_str(); }
    virtual const char* instanceType() const override { return "demo"; }

private:
    // 宿主指定的实例限定名
    std::string m_name;
    // 透传实例配置，插件自行决定是否使用
    std::string m_instanceCfg;
};

static const YomkPluginMeta g_meta = {
    YOMKPLUGIN_ABI_VERSION, "TestPlugin", "demo", "0.0.1", "Yomk", "Sample plugin for YomkPlugSys testing"};

static const YomkPluginMeta* metaFn()
{
    return &g_meta;
}

static YomkPluginInterface* createFn(const char* instance_name, const char* instance_cfg)
{
    try
    {
        if (!instance_name || !*instance_name)
        {
            // 实例名由宿主指定，必填
            return nullptr;
        }
        return new MyInstance(instance_name, instance_cfg ? instance_cfg : "");
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
