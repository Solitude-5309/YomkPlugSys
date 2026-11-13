cmake_minimum_required(VERSION 3.14)

# 编译器预选模块：g++ 优先，回退 clang++（必须在 project() 之前 include）
include(cmake/AbiInfo.cmake)
# YomkServer 版本检测模块：find_package(YomkServer) 之后调用 yomk_server_version_check()
include(cmake/YomkServerVersionCheck.cmake)

project(YomkPlugSys VERSION 2.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ABI 环境指纹：采集/打印编译环境信息并生成 abi_report.txt（project() 之后才可调用）
yomk_abi_report()

# compile_commands.json 导出到源码根目录，供 clangd 使用：
# 构建目录可任意（仓库外亦可），避免用户因 build 目录位置不同而 IDE 解析失败
add_custom_target(export_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

find_package(YomkServer REQUIRED)
# 版本检测：YomkPlugSys 依赖 YomkServer 3.6.18 及以上，低版本直接报错终止配置
yomk_server_version_check()
message(STATUS "YomkServer version: ${YomkServer_VERSION}")  
message(STATUS "YomkServer include dirs: ${YomkServer_INCLUDE_DIRS}")  
message(STATUS "YomkServer lib dir: ${YomkServer_LIB_DIR}")  
message(STATUS "YomkServer libraries: ${YomkServer_LIBRARIES}") 
include_directories(${YomkServer_INCLUDE_DIRS})
link_directories(${YomkServer_LIB_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 编译为动态库
add_library(${PROJECT_NAME} SHARED
    src/YomkManifestParser.cpp
    src/YomkPluginLoader.cpp
    src/YomkPluginManager.cpp
    src/YomkPlugSysBuilder.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE YomkServer::YomkServer ${CMAKE_DL_LIBS})
target_compile_definitions(${PROJECT_NAME} PRIVATE EXTENSION_VERSION="${PROJECT_VERSION}")

# 安装规则
# INCLUDE_INSTALL_DIR 是导出 target 的 INTERFACE include 根（<prefix>/include），与 YomkServer::YomkServer 同根，
# 对外引用统一 <yomk/plug_sys/...>；头文件物理安装位置由下方 install(DIRECTORY) 单独定为 include/yomk/plug_sys/
set(INCLUDE_INSTALL_DIR "include")
set(LIB_INSTALL_DIR "lib")

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR}
)
install(DIRECTORY include/ DESTINATION include/yomk)

# CMake export 导出配置
include(CMakePackageConfigHelpers)

install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
    PATH_VARS INCLUDE_INSTALL_DIR LIB_INSTALL_DIR
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)
