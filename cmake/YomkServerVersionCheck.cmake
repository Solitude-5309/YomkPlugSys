# ============================================================
# YomkServer 版本检测模块
#
# 用途：YomkPluginSystem 依赖 YomkServer 3.6.18 及以上（消息域 yomk::msgs /
#   payload::Response / server() 链式 API 自该版本提供），版本过低时
#   在 configure 阶段立即报错终止，阻止编译。
# 用法：find_package(YomkServer REQUIRED) 之后调用一次
#   yomk_server_version_check()
# ============================================================

# 最低支持版本：与 YomkPluginSystem 使用的框架 API 对齐
set(YOMK_SERVER_MIN_VERSION "3.6.18")

function(yomk_server_version_check)
    # YomkServer_VERSION 由 YomkServerConfig.cmake 导出（find_package 时设置）
    if(YomkServer_VERSION VERSION_LESS YOMK_SERVER_MIN_VERSION)
        message(FATAL_ERROR
            "YomkPluginSystem 依赖 YomkServer ${YOMK_SERVER_MIN_VERSION} 及以上，当前检测到版本: '${YomkServer_VERSION}'，"
            "请升级 YomkServer 后重新配置")
    endif()
endfunction()
