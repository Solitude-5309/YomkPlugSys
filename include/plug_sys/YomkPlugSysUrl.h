#pragma once
#include <string_view>

// /YomkPlugSysBuilder/* 扩展服务 URL 常量.
// 设计取舍: 沿用 SDK 内置模块风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h）——
// inline constexpr std::string_view 编译期常量,请求链全程 string_view 透传零堆分配;
// yomk::url 为开放命名空间,扩展常量按同风格直接追加,不做集中注册.
namespace yomk::url::plug_sys
{
// /YomkPlugSysBuilder/*
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
}  // namespace yomk::url::plug_sys
