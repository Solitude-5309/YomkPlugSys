# YomkPlugSys 文档

每个分片自包含：可单独打开、单独理解、单独验证，无需按顺序阅读。结构与写法遵循 [yomk文档原则](../../YomkServer/docs/yomk文档原则.md)，与源码注释（[yomk注释原则](../../YomkServer/docs/yomk注释原则.md)）配套——头文件 Doxygen 是 API 精确契约，本文档讲场景、组合与踩坑。

## 场景化入口

| 我想要… | 分片 |
|---|---|
| 5 分钟跑通：安装扩展 → 构建示例插件系统 → 内省 → 卸载 | [快速开始](quickstart.md) |
| 调用插件系统接口（构建/内省/卸载/版本查询） | [YomkPlugSysBuilder 门面服务](YomkPlugSysBuilder.md) |
| 理解插件与实例的生命周期语义（何时拒绝、何时幂等） | [插件生命周期](PluginLifecycle.md) |
| 开发一个 so 插件（ABI 契约、导出宏、实例接口） | [插件开发指南](YomkPluginInterface.md) |
| 编写插件系统清单（manifest.yomk） | [插件系统清单](PlugSysManifest.md) |
| 编译安装扩展 / 下游 CMake 集成 / 运行测试 | [构建与验证](BuildCmake.md) |

## 模块分片

| 分片 | 一句话摘要 |
|---|---|
| [YomkPlugSysBuilder](YomkPlugSysBuilder.md) | 唯一用户门面服务：清单构建驱动插件装载、聚合内省、两种卸载语义；请求经 `yomk::plug_sys()` 域入口发起 |
| [插件生命周期](PluginLifecycle.md) | 插件加载→实例创建→销毁→卸载的状态机语义：幂等边界、存活实例拒绝、限定名全局唯一主键 |
| [插件开发指南](YomkPluginInterface.md) | ABI 契约三件套：`YomkPluginMeta` 元数据、`YomkPluginInterface` 实例接口、`YOMKPLUGIN_EXPORT` 一键导出 |
| [插件系统清单](PlugSysManifest.md) | 插件系统装载清单 manifest.yomk 的语法：首行格式标识 + `实例名@动态库@实例配置` 条目 + 注释与路径规则 |
| [构建与验证](BuildCmake.md) | 交互式编译安装、下游 find_package 集成、安装布局与测试体系 |

## 规范与配套

- 术语约定：插件系统（PlugSys）= 一次清单构建所装载的插件与其实例的集合；插件（Plugin）= 单个 so 库；插件系统清单（PlugSysManifest）= 描述该系统装载内容与实例创建的 manifest.yomk；门面服务（YomkPlugSysBuilder）、机制层（Loader）/ 数据层（Manager）为内部服务；限定名 = `插件名.实例名`（全局唯一主键）；实例配置 = `instance_cfg`（透传参数）
- API 精确契约：头文件 Doxygen 注释（仓库内 `include/plug_sys/`，安装后 `<prefix>/include/yomk/plug_sys/`），逐参数与返回码以之为准
- 完整示例：`examples/ExampleYomkPlugSysBuilder.cpp`（入门全流程）、`examples/workflow/`（双插件系统清单示例）、`test/`（8 个测试闭环）
