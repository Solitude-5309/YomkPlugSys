#pragma once
#include <string_view>

// 扩展服务 URL 常量与语义描述常量.
// 设计取舍: 沿用 SDK 内置模块风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h）——
// URL 常量 inline constexpr std::string_view 编译期常量, 请求链全程 string_view 透传零堆分配;
// URL 常量集中在前, 语义描述常量集中在后按序一一对应;
// 描述常量 inline constexpr const char*, 复杂端点（副作用/状态变更）用六字段 R"(...)" 块
// （Semantic/Returns/Precond/Idempotent/SideEffect/Fail）, 纯查询端点单行;
// installFunc 第四参仅引用常量, 请求链一律引用 URL 常量不裸写字符串字面量.
// yomk::url::plug_sys 为开放命名空间, 扩展常量按同风格直接追加, 不做集中注册.
namespace yomk::url::plug_sys
{
// ---- /YomkPlugSysBuilder/*（对外门面：编排与代理） ----
inline constexpr std::string_view pluginBuild = "/YomkPlugSysBuilder/build";
inline constexpr std::string_view pluginGetVersion = "/YomkPlugSysBuilder/get_version";
inline constexpr std::string_view pluginGetBuildInfo = "/YomkPlugSysBuilder/get_build_info";
inline constexpr std::string_view pluginListPlugins = "/YomkPlugSysBuilder/list_plugins";
inline constexpr std::string_view pluginGetPlugin = "/YomkPlugSysBuilder/get_plugin";
inline constexpr std::string_view pluginListInstances = "/YomkPlugSysBuilder/list_instances";
inline constexpr std::string_view pluginGetPluginByInstance = "/YomkPlugSysBuilder/get_plugin_by_instance";
inline constexpr std::string_view pluginGetInstanceCfg = "/YomkPlugSysBuilder/get_instance_cfg";
inline constexpr std::string_view pluginUnload = "/YomkPlugSysBuilder/unload";
inline constexpr std::string_view pluginTryUnload = "/YomkPlugSysBuilder/try_unload";

// ---- /YomkPluginManager/*（内部服务：插件/实例表管理） ----
inline constexpr std::string_view mgrLoad = "/YomkPluginManager/load";
inline constexpr std::string_view mgrTryUnload = "/YomkPluginManager/try_unload";
inline constexpr std::string_view mgrForceUnload = "/YomkPluginManager/force_unload";
inline constexpr std::string_view mgrCreateInstance = "/YomkPluginManager/create_instance";
inline constexpr std::string_view mgrDestroyInstance = "/YomkPluginManager/destroy_instance";
inline constexpr std::string_view mgrList = "/YomkPluginManager/list";
inline constexpr std::string_view mgrGetPlugin = "/YomkPluginManager/get_plugin";
inline constexpr std::string_view mgrListInstances = "/YomkPluginManager/list_instances";
inline constexpr std::string_view mgrGetPluginByInstance = "/YomkPluginManager/get_plugin_by_instance";
inline constexpr std::string_view mgrGetInstanceCfg = "/YomkPluginManager/get_instance_cfg";

// ---- /YomkPluginLoader/*（内部服务：so 加载/卸载/工厂，dl 层） ----
inline constexpr std::string_view loaderLoad = "/YomkPluginLoader/load";
inline constexpr std::string_view loaderUnload = "/YomkPluginLoader/unload";
inline constexpr std::string_view loaderGetMeta = "/YomkPluginLoader/get_meta";
inline constexpr std::string_view loaderCreate = "/YomkPluginLoader/create";
inline constexpr std::string_view loaderDelete = "/YomkPluginLoader/delete";

// ---- /YomkManifestParser/*（内部服务：清单解析，无状态） ----
inline constexpr std::string_view parserParse = "/YomkManifestParser/parse";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 SDK docs/YomkServer.md「内省描述惯例」）
// /YomkPlugSysBuilder/*
inline constexpr const char* pluginBuildDesc = R"(
Semantic: 解析构建清单并编排插件加载与实例创建（清单解析委派 Parser，加载/创建委派 Manager）；同一 so 已加载时幂等跳过
Returns: BuilderInfo 包（清单路径/解析结果/已加载插件/已创建实例）
Precond: manifestPath 指向合法清单文件
Idempotent: true —— 重复 build 对已加载 so 幂等跳过，仅补载缺失项
SideEffect: 加载插件 so、创建实例并登记实例 cfgPath（仅登记不读取）；记录最近一次构建信息供 /get_build_info 查询
Fail: Invalid=请求包非 BuildReq；Failed=清单解析失败/插件加载或实例创建失败（透传内部服务原因）
)";
inline constexpr const char* pluginGetVersionDesc = "Semantic: 返回扩展版本号（CMake 编译期注入，单一来源 project(VERSION)）；Returns: String 包";
inline constexpr const char* pluginGetBuildInfoDesc = "Semantic: 返回最近一次 build 的清单路径与构建结果快照；Returns: BuilderInfo 包";
inline constexpr const char* pluginListPluginsDesc = "Semantic: 代理 Manager：列出全部已加载插件（只列举不承担按名查询）；Returns: PluginInfoList 包（总数由 d.size() 承载）";
inline constexpr const char* pluginGetPluginDesc = "Semantic: 代理 Manager：按插件名查询单条 PluginInfo；Returns: PluginInfo 包；Invalid=请求包非 String；Failed=插件未加载";
inline constexpr const char* pluginListInstancesDesc = "Semantic: 代理 Manager：列出实例信息（入参插件名可选过滤，instanceName 为限定名）；Returns: InstanceInfoList 包；Failed=指定的过滤插件未加载";
inline constexpr const char* pluginGetPluginByInstanceDesc = "Semantic: 代理 Manager：按实例限定名（插件名.实例名）反查所属插件；Returns: PluginInfo 包；Invalid=请求包非 String；Failed=实例不存在";
inline constexpr const char* pluginGetInstanceCfgDesc = "Semantic: 代理 Manager：按实例限定名读取实例配置文件内容；Returns: String 包（文件原文）；Invalid=请求包非 String；Failed=实例不存在/配置未登记/文件不可读";
inline constexpr const char* pluginUnloadDesc = R"(
Semantic: 强制卸载插件（代理 Manager force_unload）：先析构其全部实例再卸载 so
Returns: data 无负载
Precond: 插件已加载
Idempotent: false —— 未加载返回 Failed
SideEffect: 析构该插件全部实例（触发 delete_instance）并 unmap so
Fail: Invalid=请求包非 String；Failed=插件未加载/卸载失败（透传内部服务原因）
)";
inline constexpr const char* pluginTryUnloadDesc = R"(
Semantic: 尝试卸载插件（代理 Manager try_unload）：经 Loader weak 存活表全局判活，有存活持有者则拒绝
Returns: data 无负载
Precond: 插件已加载且无任何存活实例/外部持有者
Idempotent: false —— 未加载或存在存活持有者返回 Failed
SideEffect: 成功时清空实例表并 unmap so
Fail: Invalid=请求包非 String；Failed=插件未加载/存在存活实例或外部持有者
)";
// /YomkPluginManager/*
inline constexpr const char* mgrLoadDesc = R"(
Semantic: 加载插件并登记元信息（委托 Loader dlopen，meta 实时获取不缓存）
Returns: String 包（插件名）
Precond: path 指向合法插件 so；插件未加载
Idempotent: false —— 已加载返回 Failed
SideEffect: 写插件表/实例表；映射 so；meta 校验失败或插件名含 '.' 时回滚卸载 so
Fail: Invalid=请求包非 PluginLoadReq；Failed=Loader 加载失败/插件名含 '.'/插件已加载
)";
inline constexpr const char* mgrTryUnloadDesc = R"(
Semantic: 尝试卸载插件，委托 Loader weak 存活表做全局判活（覆盖 Manager 之外的持有者）
Returns: data 无负载
Precond: 插件已加载且无任何存活持有者
Idempotent: false —— 未加载或存在存活持有者返回 Failed
SideEffect: 成功时清空实例表/插件表并 unmap so
Fail: Invalid=请求包非 String；Failed=插件未加载/存在存活实例或外部持有者
)";
inline constexpr const char* mgrForceUnloadDesc = R"(
Semantic: 强制卸载插件：锁外析构其全部实例（触发 delete_instance）后再委托 Loader 卸载 so
Returns: data 无负载
Precond: 插件已加载
Idempotent: false —— 未加载返回 Failed
SideEffect: 析构该插件全部实例；写插件表/实例表；unmap so
Fail: Invalid=请求包非 String；Failed=插件未加载/Loader 卸载失败
)";
inline constexpr const char* mgrCreateInstanceDesc = R"(
Semantic: 为已加载插件创建实例，以限定名（插件名.实例名）传给插件并登记实例表
Returns: String 包（实例裸名，非限定名）
Precond: 插件已加载；instanceName 非空且组内唯一
Idempotent: false —— 每次调用创建新实例，重名返回 Failed
SideEffect: 写实例表；执行插件工厂；instanceCfgPath 仅透传登记不读取
Fail: Invalid=请求包非 CreateReq/instanceName 为空；Failed=插件未加载/实例重名/创建期间插件被卸载
)";
inline constexpr const char* mgrDestroyInstanceDesc = R"(
Semantic: 按插件名+实例裸名移出实例表，锁外析构实例（触发 delete_instance）
Returns: data 无负载
Precond: 实例存在
Idempotent: false —— 实例不存在返回 Failed
SideEffect: 写实例表；析构插件实例
Fail: Invalid=请求包非 DestroyReq；Failed=实例不存在
)";
inline constexpr const char* mgrListDesc = "Semantic: 列出全部已加载插件的 PluginInfo（只列举不承担按名查询）；Returns: PluginInfoList 包（总数由 d.size() 承载）";
inline constexpr const char* mgrGetPluginDesc = "Semantic: 按插件名查询单条 PluginInfo；Returns: PluginInfo 包；Invalid=请求包非 String；Failed=插件未加载";
inline constexpr const char* mgrListInstancesDesc = "Semantic: 列出实例信息（入参插件名可选过滤，instanceName 为限定名）；Returns: InstanceInfoList 包；Failed=指定的过滤插件未加载";
inline constexpr const char* mgrGetPluginByInstanceDesc = "Semantic: 按实例限定名反查所属插件（按组前缀+组内裸名查找，不做字符串反解析）；Returns: PluginInfo 包；Invalid=请求包非 String；Failed=实例不存在";
inline constexpr const char* mgrGetInstanceCfgDesc = "Semantic: 按实例限定名读取实例配置文件内容（创建时登记的路径）；Returns: String 包（文件原文）；Invalid=请求包非 String；Failed=实例不存在/配置未登记/文件不可读";
// /YomkPluginLoader/*
inline constexpr const char* loaderLoadDesc = R"(
Semantic: dlopen 插件 so，校验三个导出符号与 ABI 版本后登记入插件表
Returns: String 包（插件名，取自插件 meta）
Precond: path 指向含 meta/create_instance/delete_instance 导出的合法插件 so；插件未加载
Idempotent: false —— 同名插件已加载返回 Failed
SideEffect: 写插件表；映射 so 进程地址空间
Fail: Invalid=请求包非 PluginLoadReq；Failed=dlopen 失败/缺导出符号/meta 无效/ABI 不匹配/插件已加载
)";
inline constexpr const char* loaderUnloadDesc = R"(
Semantic: dlclose 插件 so；卸载前先清理失效存活项并要求存活表为空（weak 判活）
Returns: data 无负载
Precond: 插件已加载且存活实例表为空
Idempotent: false —— 未加载或有存活实例返回 Failed
SideEffect: 写插件表/存活表；unmap so（此后插件代码不可执行）
Fail: Invalid=请求包非 String；Failed=插件未加载/仍有存活实例
)";
inline constexpr const char* loaderGetMetaDesc = "Semantic: 实时调用插件 meta 导出函数返回元信息（不缓存）；Returns: PluginInfo 包；Invalid=请求包非 String；Failed=插件未加载/meta 无效";
inline constexpr const char* loaderCreateDesc = R"(
Semantic: 调用插件工厂创建实例（限定名+cfgPath 透传），登记存活表（weak 引用）后随响应返回
Returns: PluginInstance 包（持有实例智能指针，deleter 绑定该库 delete_instance）
Precond: 插件已加载
Idempotent: false —— 每次调用产生新实例
SideEffect: 写存活表；持锁执行插件工厂代码（与 unload 的 dlclose 互斥）
Fail: Invalid=请求包非 CreateReq；Failed=插件未加载/工厂返回空
)";
inline constexpr const char* loaderDeleteDesc = R"(
Semantic: 销毁传入实例（入参拷贝随请求结束释放，触发 delete_instance），并回收存活表中的失效项；实例引用归 Manager 持有
Returns: data 无负载
Idempotent: true —— 重复调用仅重复触发失效项回收
SideEffect: 写存活表；析构传入实例
Fail: Invalid=请求包非 PluginInstance
)";
// /YomkManifestParser/*
inline constexpr const char* parserParseDesc = "Semantic: 解析构建清单文件为结构化结果（纯解析无状态）；Returns: ManifestParseResult 包（dir=清单所在目录，entries=实例块表）；Invalid=请求包非 BuildReq；Failed=清单不可读/格式非法";
}  // namespace yomk::url::plug_sys
