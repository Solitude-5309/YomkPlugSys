# YomkPluginSystem 扩展

基于 [YomkServer](https://github.com/Solitude-5309/YomkServer) 框架的动态插件系统扩展：运行时加载 so 插件、管理插件实例生命周期、提供内省与两种卸载语义。本库为独立设计，不依赖任何第三方插件框架。

## 架构速览

| 服务 | 定位 | 职责 |
|------|------|------|
| `/YomkPluginSystemBuilder` | 编排层（唯一用户门面） | 解析 manifest.yomk 清单驱动构建、聚合内省、卸载代理；托管内部服务生命周期 |
| `/YomkPluginLoader` | 机制层（内部服务） | dlopen/dlsym/dlclose、meta 读取、实例创建机制；不登记插件、不拥有实例 |
| `/YomkPluginManager` | 数据层（内部服务） | 插件表 + 实例表（shared_ptr 唯一所有者）、限定名管理、生命周期管理 |

三服务纯请求通信。用户仅注册/请求门面服务，接口一律经 `yomk::plugin()` 链式域入口发起（通用 `url/msg/request` 链归 `yomk::server()`）；内部服务 URL 仅供扩展内部与白盒测试直调。

## 快速开始

```bash
source build_ubuntu.sh    # 交互式：输入 YomkServer 安装路径与扩展安装路径（默认均取 $YOMK_PREFIX_PATH）
```

安装后以最小程序跑通（构建示例插件工程 → 内省 → 卸载）：

```cpp
#include <yomk/plugin_system/YomkPluginApi.h>
#include <iostream>

using namespace yomk;

int main()
{
    yomk::server().init();

    // 注册唯一门面服务（内部自动托管 Loader/Manager 生命周期）
    yomk::server().srv<YomkPluginSystemBuilder>().create();

    // 按清单构建插件工程（清单语法见 docs/PluginManifest.md）
    auto build = yomk::plugin().build("/abs/path/to/manifest.yomk");

    // 内省：插件列表
    auto plugins = yomk::plugin().infoPlugins();
    auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(plugins.data);
    if (arr)
        for (const auto& m : arr->d)
            std::cout << "plugin: " << m.name << " v" << m.version << std::endl;

    // 尝试卸载（有存活实例拒绝；强制卸载用 unload）
    auto r = yomk::plugin().tryUnload("ConnectionService");
    std::cout << "tryUnload: " << r.msg << std::endl;
    return 0;
}
```

可直接运行随扩展安装的入门示例观察全流程输出：`<安装路径>/bin/ExampleYomkPluginSystemBuilder`。

## 文档

按需跳跃阅读，每个分片自包含（遵循 [yomk文档原则](../YomkServer/docs/yomk文档原则.md)）：

| 我想要… | 分片 |
|---|---|
| 5 分钟跑通全流程 | [快速开始](docs/quickstart.md) |
| 查接口用法（构建/内省/卸载/版本） | [门面服务](docs/YomkPluginSystemBuilder.md) |
| 理解生命周期语义（何时拒绝、何时幂等） | [插件生命周期](docs/PluginLifecycle.md) |
| 开发一个 so 插件 | [插件开发指南](docs/YomkPluginInterface.md) |
| 编写 manifest.yomk 清单 | [清单格式](docs/PluginManifest.md) |
| 编译安装 / CMake 集成 / 运行测试 | [构建与验证](docs/BuildCmake.md) |

完整分片目录见 [docs/README.md](docs/README.md)。

## 工程结构

```
YomkPluginSystem/
├── include/
│   └── plugin_system/         # 扩展目录名（安装后对外引用统一 <yomk/plugin_system/...>，随 install 分发）
│       ├── YomkPluginInterface.h     # ABI：插件侧契约（元数据 + 实例接口 + 导出宏）
│       ├── YomkPluginUrl.h           # 模块 URL 常量（yomk::url，plugin 前缀）
│       ├── YomkPluginPayload.h       # 模块专属载荷（yomk::payload 数据结构体）
│       ├── YomkPluginMsg.h           # 模块专属消息包（YOMK_DEFINE_MSG 注册）
│       ├── YomkPluginSystemBuilder.h # 编排层服务声明（唯一用户服务）
│       └── YomkPluginApi.h           # 模块 API 聚合头（四件套之 Api：唯一对外入口，含 yomk::plugin() 域入口）
├── src/                          # 内部实现（不随 install 分发）
│   ├── YomkPluginLoader.h        # 机制层服务声明（含宿主 dlsym 契约）
│   ├── YomkPluginManager.h       # 数据层服务声明
│   ├── YomkPluginLoader.cpp      # 机制层服务
│   ├── YomkPluginManager.cpp     # 数据层服务
│   └── YomkPluginSystemBuilder.cpp # 编排层服务
├── docs/                         # 文档（分片自包含，入口见 docs/README.md）
├── examples/                     # 演示示例
│   ├── ExampleYomkPluginSystemBuilder.cpp  # 入门示例（随扩展默认编译安装）
│   └── workflow/                 # workflow 示例插件模块 + 清单
├── test/                         # 测试（8 闭环 + 一键运行器）
│   ├── test_plugin/              # 示例插件（SHARED 库）
│   ├── bad_plugin/ builder/ concurrency/ facade/  # 分目录测试源码
│   └── run_tests.sh / run_static_checks.sh
├── cmake/
│   ├── AbiInfo.cmake             # 编译器预选 + ABI 环境指纹
│   ├── YomkServerVersionCheck.cmake # YomkServer 版本检测（3.6.18 及以上）
│   └── ProjectConfig.cmake.in    # find_package 导出配置模板
├── CMakeLists.txt
├── build_ubuntu.sh               # 一键编译脚本（交互式）
└── README.md
```

## 测试

```bash
cmake -S test -B test/build -DCMAKE_PREFIX_PATH="${YOMK_PREFIX_PATH}" -DCMAKE_BUILD_TYPE=Release
cmake --build test/build -j
./test/run_tests.sh              # 8 个测试闭环，失败即停，日志落盘 test/test_logs/
```

测试体系与静态检查的完整说明见[构建与验证](docs/BuildCmake.md)。

## License

[Apache License 2.0](LICENSE)
