#pragma once
#include <string_view>

/*
 * /YomkPluginSystemBuilder/* 扩展服务 URL 常量：与 SDK 内置模块同风格
 * （参考 yomk/fn_pool/YomkFunctionPoolUrl.h），inline constexpr std::string_view
 * 编译期常量，请求链全程 string_view 透传，调用路径零堆分配。
 * yomk::url 为开放命名空间：扩展的 url 常量按同风格直接追加。
 */
namespace yomk::url
{
// /YomkPluginSystemBuilder/*
inline constexpr std::string_view pluginBuild = "/YomkPluginSystemBuilder/build";
inline constexpr std::string_view pluginVersion = "/YomkPluginSystemBuilder/version";
inline constexpr std::string_view pluginBuilderInfo = "/YomkPluginSystemBuilder/builder";
inline constexpr std::string_view pluginListPlugins = "/YomkPluginSystemBuilder/plugins";
inline constexpr std::string_view pluginListInstances = "/YomkPluginSystemBuilder/instances";
inline constexpr std::string_view pluginByInstance = "/YomkPluginSystemBuilder/plugin_by_instance";
inline constexpr std::string_view pluginInstancesFile = "/YomkPluginSystemBuilder/instances_file";
inline constexpr std::string_view pluginUnload = "/YomkPluginSystemBuilder/unload";
inline constexpr std::string_view pluginTryUnload = "/YomkPluginSystemBuilder/try_unload";
}  // namespace yomk::url
