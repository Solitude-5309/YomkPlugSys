# 快速开始

5 分钟跑通 YomkPluginSystem：安装扩展 → 写最小宿主程序 → 构建示例插件工程 → 内省 → 卸载。全程约 3 段命令 + 1 个程序文件。

## 适用与不适用

✅ 适合：
- 首次接触本扩展，想最快看到插件装载与内省效果

❌ 不适合：
- 已完成安装、直接查接口用法（见[门面服务](YomkPluginSystemBuilder.md)）

> 前置：[YomkServer](../../YomkServer/docs/README.md) 已安装（默认前缀 `/opt/yomk`，环境变量 `YOMK_PREFIX_PATH` 指向它）。

## 第 1 步：编译安装扩展

```bash
cd YomkPluginSystem
source build_ubuntu.sh
# 依次输入 YomkServer 安装路径（默认 $YOMK_PREFIX_PATH）与扩展安装路径（默认同前缀）
```

安装完成后扩展 so 与头文件进入同一前缀（安装布局详见[构建与验证](BuildCmake.md)）。

## 第 2 步：最小宿主程序

保存为 `main.cpp`：

```cpp
#include <yomk/plugin_system/YomkPluginApi.h>
#include <iostream>

using namespace yomk;

int main()
{
    yomk::server().init();

    // 注册唯一门面服务（内部自动托管 Loader/Manager 生命周期）
    yomk::server().srv<YomkPluginSystemBuilder>().create();

    // 按清单构建示例插件工程（清单见 examples/workflow/manifest.yomk）
    auto build = yomk::plugin().build(PLUGIN_MANIFEST);
    std::cout << "build: " << build.msg << std::endl;

    // 内省：插件列表
    auto plugins = yomk::plugin().infoPlugins();
    auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(plugins.data);
    if (arr)
        for (const auto& m : arr->d)
            std::cout << "plugin: " << m.name << " v" << m.version << std::endl;

    // 卸载（有存活实例拒绝；强制卸载用 unload）
    auto r = yomk::plugin().tryUnload("ConnectionService");
    std::cout << "tryUnload: " << r.msg << std::endl;
    return 0;
}
```

编译运行（清单绝对路径经 `PLUGIN_MANIFEST` 宏编译期注入）：

```bash
MANIFEST="$PWD/examples/workflow/manifest.yomk"
g++ -std=c++17 main.cpp -o demo -DPLUGIN_MANIFEST="\"$MANIFEST\"" \
    -I"${YOMK_PREFIX_PATH}/include" \
    -L"${YOMK_PREFIX_PATH}/lib" -lYomkPluginSystem -lYomkServer -Wl,-rpath,"${YOMK_PREFIX_PATH}/lib"

./demo
```

预期输出（节选）：`build: ok (plugins:2 instances:2)`、两行 `plugin: ConnectionService/WorkspaceService`、`tryUnload: ok`。

## 第 3 步：观察完整流程

运行随扩展安装的入门示例（每一步注释标明【接口 / 输入 / 输出】）：

```bash
<安装路径>/bin/ExampleYomkPluginSystemBuilder
```

## 下一步

- 自己写一个插件：[插件开发指南](YomkPluginInterface.md)
- 修改装载清单：[清单格式](PluginManifest.md)
- 理解卸载拒绝/幂等边界：[插件生命周期](PluginLifecycle.md)
- 下游 CMake 工程集成：[构建与验证](BuildCmake.md)
