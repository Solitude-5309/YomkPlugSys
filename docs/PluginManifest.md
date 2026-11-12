# 清单格式（manifest.yomk）

manifest.yomk 是插件工程的装载清单：描述要装载哪些 so 插件、为每个插件创建哪些实例。Builder 的构建入口以清单路径为唯一输入（`yomk::plugin().manifest(path).build()`），按行解析并逐条装载。文件名不限定（`.yomk` 后缀文件因用处不同格式各异，**以首行格式标识区分**）。

## 适用与不适用

✅ 适合：
- 编写/维护插件工程清单（批量装载入口）
- 排查 `build` 返回的清单行号报错

❌ 不适合：
- 单插件编程式装载（走 `PluginPath` 请求，不经清单）
- 插件本体的开发（见[插件开发指南](YomkPluginInterface.md)）

> 前置概念：清单条目中的实例名是**裸名**（同插件内唯一），系统登记后自动拼接为限定名 `插件名.实例名`（见[插件生命周期](PluginLifecycle.md)）。

## 最小可运行示例

以下为 `examples/workflow/manifest.yomk` 原文（双插件工程，随示例程序可构建验证）：

```
#! yomk_plugin_system

# 连接器配置
ConnectionServiceIns@ConnectionService/lib/libConnectionService.so@ConnectionService/instances/ConnectionService.txt

# 工作空间配置
WorkspaceServiceIns@WorkspaceService/lib/libWorkspaceService.so@WorkspaceService/instances/WorkspaceService.txt
```

预期行为：`yomk::plugin().manifest("<路径>/manifest.yomk").build()` 返回 `plugins:2 instances:2`；两实例登记为 `ConnectionService.ConnectionServiceIns`、`WorkspaceService.WorkspaceServiceIns`。

## 语法规则

**首行**必须为格式标识（缺失、不在首行或不匹配则整体解析失败）：

```
#! yomk_plugin_system
```

**条目行**按 `@` 切分为三段：

```
实例名@动态库相对路径@实例配置文件相对路径
```

| 段 | 规则 |
|---|---|
| 实例名 | 裸名，同一插件内唯一；空名解析失败；不得含 `.`（限定名分隔符，查找接口按 `插件名.` 前缀匹配） |
| 动态库路径 | 相对**清单所在目录**的完整路径，须含平台相关文件名全名（Linux `libX.so` / macOS `libX.dylib` / Windows `X.dll`）；拒绝绝对路径 |
| 实例文件路径 | 相对**清单所在目录**的完整路径，**必填**且文件须存在；拒绝绝对路径 |

**注释与空行**：`#` 开头的整行为注释；条目行中 `#` 之后为行内注释；两者解析时均忽略，空行跳过。

## build 流程

解析清单 → 校验实例配置文件/动态库存在 → 逐条装载（同一 so 路径已加载则复用，不重复 load）→ 按清单实例名创建实例。任一步失败返回 `Failed` 并**指明清单行号**；已成功条目不回滚（详见[插件生命周期](PluginLifecycle.md)）。

## 注意事项

- 路径一律相对**清单所在目录**解析，与当前工作目录无关；`<模块>/lib/`、`<模块>/instances/` 仅为示例工程的目录组织约定，Builder 不强制。
- 跨平台部署时各平台使用各自清单文件（库名含平台后缀，清单不代做转换）；目录布局与库名完全解耦。
- 实例文件内容 Builder 不读取，仅把其绝对路径作为 `instance_file` 透传给插件工厂（见[插件开发指南](YomkPluginInterface.md)）；但**文件存在性会校验**。
- 同一清单中同一插件装载多条（不同实例）合法：装载幂等，实例各自创建。

## 常见问题 FAQ

- **清单可以用别的文件名吗？** 可以，格式标识只认首行 `#! yomk_plugin_system`；`.yomk` 后缀仅为约定。
- **条目失败会怎样？** 中止并返回失败原因与行号；之前条目已装载的不回滚，修正后重新 `build` 即可。
- **实例文件能省略吗？** 清单里不能：三段均必填，任意一段为空报 `empty segment`；文件不存在报 `instance file not found`。直接请求 `/YomkPluginManager/create_instance` 时 `instance_file` 可为空（插件工厂收到空串）。
- **相对路径以哪里为基准？** 只以清单文件所在目录为基准，与 CWD、安装路径均无关。

## 相关跳转

- [门面服务](YomkPluginSystemBuilder.md)：`build` 接口的调用方式与 `BuilderInfo` 内省
- [插件生命周期](PluginLifecycle.md)：装载/创建的幂等与失败语义
- [插件开发指南](YomkPluginInterface.md)：清单指向的 so 需满足的契约

## 完整示例

清单 + 两个示例插件模块 + 实例文件的完整工程见 `examples/workflow/`（`manifest.yomk` + `ConnectionService/` + `WorkspaceService/`），经 `examples/ExampleYomkPluginSystemBuilder.cpp` 一键构建验证。
