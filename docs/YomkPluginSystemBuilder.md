# YomkPluginSystemBuilder（门面服务）

YomkPluginSystem 的唯一用户门面服务：解析 manifest.yomk 清单驱动插件装载与实例创建、聚合内省、提供两种卸载语义。注册该服务即自动托管内部服务（机制层 Loader / 数据层 Manager）的注册与注销。全部请求经链式域入口 `yomk::plugin()` 发起，阻塞至服务应答。

## 适用与不适用

✅ 适合：
- 运行时按清单批量装载 so 插件并创建实例（自动化编排入口）
- 查询已加载插件 / 存活实例 / 最近构建信息
- 卸载插件（强制 / 有存活实例拒绝两种语义）

❌ 不适合：
- 单个实例的细粒度创建销毁（内部服务 `/YomkPluginManager/*` 仅白盒测试专用，见[生命周期](PluginLifecycle.md)）
- 跨进程使用（消息包经 shared_ptr 进程内持有传递，无序列化）
- 插件 so 的业务逻辑实现（见[插件开发指南](YomkPluginInterface.md)）

> 前置概念：[消息包](../../YomkServer/docs/YomkMsg.md)——入参与返回负载均为强类型消息包；服务注册方式见[快速开始](quickstart.md)。

## 域入口速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 清单构建 | `yomk::plugin().manifest(workflowPath).build()` | → `Response`（data 为 `String`：`plugins:N instances:M` 汇总） |
| 强制卸载 | `yomk::plugin().plg_name(pluginName).unload()` | → `Response`（先清空该插件实例表再卸载；外部引用未释放时 `Failed`） |
| 尝试卸载 | `yomk::plugin().plg_name(pluginName).try_unload()` | → `Response`（有存活实例 `Failed`，插件保持加载） |
| 最近构建 | `yomk::plugin().builder()` | → `Response`（data 为 `BuilderInfo`） |
| 插件列表 | `yomk::plugin().plugins()` | → `Response`（data 为 `PluginMetaArray`） |
| 插件信息 | `yomk::plugin().plg_name(pluginName).get_info()` | → `Response`（data 为 `PluginMeta` 单条插件信息；未知插件 `Failed`） |
| 实例列表 | `yomk::plugin().instances()` | → `Response`（data 为 `InstanceInfoArray` 全量实例明细） |
| 插件实例 | `yomk::plugin().plg_name(pluginName).instances()` | → `Response`（data 为 `InstanceInfoArray`，仅该插件下的实例；未知插件 `Failed`） |
| 实例查插件 | `yomk::plugin().inst_name(instanceName).get_plg_info()` | → `Response`（data 为 `PluginMeta`；未知实例 `Failed`） |
| 实例配置 | `yomk::plugin().inst_cfg(instanceName)` | → `Response`（data 为 `String` 文件内容；未知实例/不可读 `Failed`） |
| 版本查询 | `yomk::plugin().version()` | → `Response`（data 为 `String`：`YomkPluginSystem vX.Y.Z (WIP)`） |

> `instanceName` 一律为限定名 `插件名.实例名`（全局唯一主键）；`pluginName` 为插件 meta 名。

## 使用示例

```cpp
#include <yomk/plugin_system/YomkPluginApi.h>
using namespace yomk;

yomk::server().init();
yomk::server().srv<YomkPluginSystemBuilder>().create();

// 按清单构建（manifest 绑定清单路径，build 终结；清单格式见 PluginManifest.md；任一步失败 Failed 并指明清单行号）
auto build = yomk::plugin().manifest("examples/workflow/manifest.yomk").build();

// 内省：插件列表与实例列表
auto plugins = yomk::plugin().plugins();
auto instances = yomk::plugin().instances();                        // 全量实例明细
auto one = yomk::plugin().plg_name("ConnectionService").instances(); // 仅该插件下的实例

// 卸载（有存活实例时 try_unload 拒绝；unload 先清空实例表再卸载，外部引用未释放同样拒绝）
auto r = yomk::plugin().plg_name("ConnectionService").try_unload();
```

结果一律在 `Response.data` 中，按返回类型解包：

```cpp
auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(plugins.data);
if (arr)
{
    for (const auto& m : arr->d)
        std::cout << m.name << " v" << m.version << std::endl;
}
```

> `unpack` 类型不匹配或 data 为空返回 `nullptr`（不抛异常），使用前须判空。

## 注意事项

- `build` 流程：解析清单 → 校验实例配置与动态库存在 → 加载（同一 so 路径已加载则幂等复用；另一个 so 文件导出同名 meta 会被拒绝）→ 按清单实例名创建（登记为限定名）。任一步失败 `Failed` 并指明清单行号（清单不可读、首行标识缺失同样失败）。
- `unload` 与 `try_unload` 是两种语义：前者先清空该插件实例表再卸载（不等待实例自然销毁）；后者不动实例、有存活实例即拒绝。两者的最终卸载都经 Loader 判活，外部仍持有实例引用时都会 `Failed`（`instances still alive`），此时 `unload` 已清空实例表、插件仍加载。边界详见[插件生命周期](PluginLifecycle.md)。
- `BuilderInfo` 反映**最近一次**构建（清单路径/结果/条目行/插件与实例数），空值 = 尚未构建；多次构建只保留最后一次。
- 门面服务注册后即可请求；未注册时请求返回 `Failed`。
- 版本值由 CMake 编译期注入（`project(VERSION)` 单一来源），运行时查询返回形如 `YomkPluginSystem v1.0.7 (WIP)` 的描述（版本号随工程版本变化）。

## 常见问题 FAQ

- **请求阻塞吗？** 阻塞至服务应答（同步语义）；无异步接口。
- **build 重复执行同一清单？** 已加载插件幂等跳过；实例重名拒绝，返回 `Failed` 并指明行号。
- **消息类型发错了会怎样？** 服务侧返回 `Failed`（`invalid request msg type`），报出期望与实际类型名。

## 相关跳转

- [插件生命周期](PluginLifecycle.md)：各接口的状态机语义与失败分支
- [清单格式](PluginManifest.md)：`build` 入参文件的完整语法
- [插件开发指南](YomkPluginInterface.md)：被装载 so 的开发规范

## 完整示例

完整可运行演示见 `examples/ExampleYomkPluginSystemBuilder.cpp`（随扩展默认编译安装到 `<安装路径>/bin`）：以 `examples/workflow` 的两个示例插件串起 初始化 → 构建（含构建信息）→ 内省（插件/实例/按插件过滤/按实例查插件/实例配置）→ 卸载 全流程，每一步注释标明【接口 / 输入 / 输出】，运行即可观察实际结果。
