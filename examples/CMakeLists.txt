cmake_minimum_required(VERSION 3.14)
project(YomkPlugSysExamples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(YomkServer REQUIRED)
find_package(YomkPlugSys REQUIRED)

message(STATUS "YomkPlugSys version: ${YomkPlugSys_VERSION}")
include_directories(${YomkPlugSys_INCLUDE_DIRS})

# workflow 示例插件：产物输出到 examples/workflow 各模块 lib/，供 Builder 运行时加载
add_subdirectory(workflow/ConnectionService ConnectionService)
add_subdirectory(workflow/WorkspaceService WorkspaceService)

# Builder 演示程序：清单路径编译期固化到 workflow 源码目录
add_executable(ExampleYomkPlugSysBuilder ExampleYomkPlugSysBuilder.cpp)
target_compile_definitions(ExampleYomkPlugSysBuilder PRIVATE
    WORKFLOW_MANIFEST_PATH="${CMAKE_CURRENT_SOURCE_DIR}/workflow/manifest.yomk"
)
target_link_libraries(ExampleYomkPlugSysBuilder PRIVATE
    YomkPlugSys::YomkPlugSys YomkServer::YomkServer
)

# 演示程序随主库安装到 <安装路径>/bin（插件 .so 保留在源码树 workflow 模块 lib/，不安装）
install(TARGETS ExampleYomkPlugSysBuilder RUNTIME DESTINATION bin)
