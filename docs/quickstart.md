# 快速开始

5 分钟跑通 YomkPlugSys：安装扩展 → 写最小宿主工程 → 构建示例插件系统 → 内省 → 卸载。全程约 3 段命令 + 2 个小文件（`main.cpp` / `CMakeLists.txt`）。

## 适用与不适用

✅ 适合：
- 首次接触本扩展，想最快看到插件装载与内省效果

❌ 不适合：
- 已完成安装、直接查接口用法（见[门面服务](YomkPlugSysBuilder.md)）

> 前置：[YomkServer](../../YomkServer/docs/README.md) 已安装（默认前缀 `/opt/yomk`，环境变量 `YOMK_PREFIX_PATH` 指向它）。

## 第 1 步：编译安装扩展

```bash
cd YomkPlugSys
source build_ubuntu.sh
# 依次输入 YomkServer 安装路径（默认 $YOMK_PREFIX_PATH）与扩展安装路径（默认同前缀）
```

安装完成后扩展 so 与头文件进入同一前缀（安装布局详见[构建与验证](BuildCmake.md)）。

## 第 2 步：最小宿主工程

新建空目录（如 `~/yomk-demo`，避免污染本仓库）存放下面两个文件。

`main.cpp`：

```cpp
#include <yomk/plug_sys/YomkPluginApi.h>
#include <iostream>

using namespace yomk;

int main()
{
    yomk::server().init();

    // 注册唯一门面服务（内部自动托管 Loader/Manager 生命周期）
    yomk::server().srv<YomkPlugSysBuilder>().create();

    // 按清单构建示例插件系统（清单见 examples/workflow/manifest.yomk）
    auto build = yomk::plugin().manifest(PLUGIN_MANIFEST).build();
    auto summary = yomk::msgs::unpack<yomk::msgs::String>(build.data);   // 汇总在 data 的 String 包内
    std::cout << "build: " << build.msg << " (" << (summary ? summary->d : "") << ")" << std::endl;

    // 内省：插件列表
    auto plugins = yomk::plugin().plugins();
    auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(plugins.data);
    if (arr)
        for (const auto& m : arr->d)
            std::cout << "plugin: " << m.name << " v" << m.version << std::endl;

    // 卸载（有存活实例拒绝；强制卸载用 unload）
    auto r = yomk::plugin().plg_name("ConnectionService").try_unload();
    std::cout << "try_unload: " << r.msg << std::endl;
    return 0;
}
```

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.14)
project(demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)
find_package(YomkPlugSys REQUIRED)

add_executable(demo main.cpp)
target_compile_definitions(demo PRIVATE PLUGIN_MANIFEST="${PLUGIN_MANIFEST}")
target_link_libraries(demo PRIVATE YomkPlugSys::YomkPlugSys YomkServer::YomkServer)
```

构建运行（清单绝对路径经 `PLUGIN_MANIFEST` 缓存变量注入宏；`YomkPlugSys` 对 YomkServer 是 PRIVATE 链接，故宿主须显式再链 `YomkServer::YomkServer`）：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="${YOMK_PREFIX_PATH}" \
    -DPLUGIN_MANIFEST="/path/to/YomkPlugSys/examples/workflow/manifest.yomk"
cmake --build build -j
./build/demo
```

预期输出（节选）：`build: ok (plugins:2 instances:2)`、两行 `plugin: ConnectionService v1.0.0` 与 `plugin: WorkspaceService v1.0.0`、`try_unload: instances still alive: ConnectionService`（实例仍存活，保守语义拒绝卸载）。

## 第 3 步：观察完整流程

运行随扩展安装的入门示例（每一步注释标明【接口 / 输入 / 输出】）：

```bash
<安装路径>/bin/ExampleYomkPlugSysBuilder
```

> 示例程序的清单路径在编译期固化为源码树中的 `examples/workflow/manifest.yomk`，示例插件 so 也留在源码树（不随安装分发），因此需在本仓库原位置运行。

## 下一步

- 自己写一个插件：[插件开发指南](YomkPluginInterface.md)
- 修改插件系统清单：[插件系统清单](PlugSysManifest.md)
- 理解卸载拒绝/幂等边界：[插件生命周期](PluginLifecycle.md)
- 下游 CMake 工程集成：[构建与验证](BuildCmake.md)
