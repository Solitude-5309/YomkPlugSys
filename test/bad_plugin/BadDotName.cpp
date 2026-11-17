// BadDotName：meta 合法但插件名含 '.'。
// 预期 Manager load 拒绝："plugin name must not contain '.'"（限定名拼接唯一性防护）。
#include <yomk/plug_sys/YomkPluginInterface.h>

static const YomkPluginMeta k_meta = {
    YOMK_PLUGIN_ABI_VERSION, "Bad.Dot.Name", "bad", "0.0.1", "Yomk", "plugin name contains dot",
};

static YomkPluginInterface* createNull(const char*, const char*)
{
    return nullptr;
}

static void deleteNothing(YomkPluginInterface*)
{
}

YOMK_PLUGIN_EXPORT([] { return &k_meta; }, createNull, deleteNothing)
