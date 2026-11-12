#pragma once

// 插件侧 ABI 契约（单文件承载）.
// 设计取舍:
// 1. 元数据 C 结构与三个导出符号共用本头:插件作者只依赖一个文件即可完成导出,
//    宿主侧仅额外持有 dlsym 用的符号名宏（src/YomkPluginLoader.h）;
// 2. YomkPluginMeta 跨 dlsym 边界且由宿主按 C 语义读取,故保持 POD + 裸 char 指针;
// 3. ABI 版本是独立常量,不与 project(VERSION) 绑定:仅当 YomkPluginMeta 布局或导出
//    符号签名变化时递增.历史:v2 为实例接口虚表布局变化（删除 instanceId）.

/* ------------------------- 插件元数据（C 兼容） ------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief ABI contract version expected by host and plugin
 * @note Independent of the project version; bumped only when the YomkPluginMeta layout
 *       or the export symbol signatures change. A mismatch rejects the load.
 */
// @zh: 宿主与插件共同约定的 ABI 契约版本。
// @zh: note 独立于工程版本号，仅当 YomkPluginMeta 布局或导出符号签名变化时递增；不匹配则加载被拒绝。
#define YOMKPLUGIN_ABI_VERSION 2

    /**
     * @brief Static plugin metadata returned by the meta export function
     * @note All string members are read-only during the plugin lifetime.
     */
    // @zh: 插件元数据，由 meta 导出函数返回的静态常量。
    // @zh: note 字符串成员在插件存活期内只读。
    struct YomkPluginMeta
    {
        int abi_version;            // ABI 契约版本
        const char* name;           // 插件名（全局唯一，不得含 '.'）
        const char* type;           // 插件类型
        const char* version;        // 插件版本
        const char* author;         // 作者
        const char* description;    // 说明
    };

#ifdef __cplusplus
}
#endif

/* ------------------------- 插件实例接口 ------------------------- */

/**
 * @brief Abstract interface of a plugin instance
 * @warning Host and plugin must be built with the same compiler and this same header;
 *          crossing distinct standard libraries or compiler ABIs is undefined behavior.
 */
// @zh: 插件实例抽象接口。
// @zh: warning 宿主与插件须使用同一编译器与本头文件构建；跨标准库或编译器 ABI 属未定义行为。
class YomkPluginInterface
{
public:
    virtual ~YomkPluginInterface() = default;

    /**
     * @brief Get the instance name
     * @return Fully qualified instance name assigned by the host (pluginName.instanceName)
     * @note Registration, lookup and introspection all key on this name;
     *       the returned pointer must stay valid while the instance is alive.
     */
    // @zh: 获取实例名。
    // @zh: return 宿主指定的限定名（插件名.实例名）。
    // @zh: note 登记、查找与内省均以此名为键；返回指针须在实例存活期内有效。
    [[nodiscard]] virtual const char* instanceName() const = 0;

    /**
     * @brief Get the instance type
     * @return Instance type string owned by the plugin
     */
    // @zh: 获取实例类型。
    // @zh: return 插件提供的类型字符串。
    [[nodiscard]] virtual const char* instanceType() const = 0;

    void* userData = nullptr;   // 用户数据，默认为空
};

/* ------------------------- 插件导出契约 ------------------------- */

/**
 * @brief Export the three fixed C symbols a plugin must provide
 * @param MetaFn Function returning the static metadata pointer
 * @param CreateFn Function creating an instance from (instance_name, instance_file)
 * @param DeleteFn Function destroying an instance
 * @note The host resolves the symbols below through dlsym; plugins only use this macro:
 *       const YomkPluginMeta* yomk_plugin_meta();
 *       YomkPluginInterface* yomk_plugin_create_instance(const char* instance_name, const char* instance_file);
 *       void yomk_plugin_delete_instance(YomkPluginInterface* instance);
 * @note instance_name is the fully qualified name chosen by the host and must be adopted as is;
 *       instance_file is an opaque pass-through parameter (may be empty) that the framework never reads.
 * @warning All three functions must be exception-safe; catch on the plugin side and
 *          return nullptr from create on failure.
 */
// @zh: 导出插件必须提供的三个固定 C 符号。
// @zh: param MetaFn 返回静态元数据指针的函数。
// @zh: param CreateFn 由（实例名，实例文件）创建实例的函数。
// @zh: param DeleteFn 销毁实例的函数。
// @zh: note 宿主经 dlsym 解析下述符号，插件只需使用本宏：
// @zh:       const YomkPluginMeta* yomk_plugin_meta();
// @zh:       YomkPluginInterface* yomk_plugin_create_instance(const char* instance_name, const char* instance_file);
// @zh:       void yomk_plugin_delete_instance(YomkPluginInterface* instance);
// @zh: note instance_name 为宿主指定的限定名，插件须原样采用；instance_file 为不透明透传参数
// @zh:      （可为空），框架不读取不解析。
// @zh: warning 三个函数均须异常安全：插件侧自行捕获，创建失败返回空指针。
#define YOMKPLUGIN_EXPORT(MetaFn, CreateFn, DeleteFn)                                                                 \
    extern "C" const YomkPluginMeta* yomk_plugin_meta()                                                               \
    {                                                                                                                 \
        return MetaFn();                                                                                              \
    }                                                                                                                 \
    extern "C" YomkPluginInterface* yomk_plugin_create_instance(const char* instance_name, const char* instance_file) \
    {                                                                                                                 \
        return CreateFn(instance_name, instance_file);                                                                \
    }                                                                                                                 \
    extern "C" void yomk_plugin_delete_instance(YomkPluginInterface* instance)                                        \
    {                                                                                                                 \
        DeleteFn(instance);                                                                                           \
    }
