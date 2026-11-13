// SecondPlugin：第二个可加载示例插件（仅测试用，不安装）
// 与 TestPlugin 同构、meta 名不同，覆盖跨插件同名实例场景：
// 实例规范名为宿主拼接的限定名（插件名.实例名，全局唯一），跨插件允许同名裸名。
#include <yomk/plugin_system/YomkPluginApi.h>

#include <string>

class SecondInstance : public YomkPluginInterface
{
public:
    SecondInstance(const std::string& name, const std::string& instanceCfg) : m_name(name), m_instanceCfg(instanceCfg) {}
    virtual ~SecondInstance() {}

    virtual const char* instanceName() const override { return m_name.c_str(); }
    virtual const char* instanceType() const override { return "demo"; }

private:
    // 宿主指定的实例限定名
    std::string m_name;
    // 透传实例配置，插件自行决定是否使用
    std::string m_instanceCfg;
};

static const YomkPluginMeta g_meta = {
    YOMKPLUGIN_ABI_VERSION, "SecondPlugin", "demo", "0.0.1", "Yomk", "Second loadable plugin for cross-plugin tests"};

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
        return new SecondInstance(instance_name, instance_cfg ? instance_cfg : "");
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
