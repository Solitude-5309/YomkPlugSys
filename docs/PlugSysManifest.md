# 插件系统清单（manifest.yomk）

manifest.yomk 是插件系统（PlugSys）的装载清单：描述该系统要装载哪些 so 插件、为每个插件创建哪些实例。Builder 的构建入口以清单路径为唯一输入（`yomk::plug_sys().manifest(path).build()`），按实例块解析并逐块装载。文件名不限定（`.yomk` 后缀文件因用处不同格式各异，**以首行格式标识区分**）。

## 适用与不适用

✅ 适合：
- 编写/维护插件系统清单（批量装载入口）
- 排查 `build` 返回的清单行号报错

❌ 不适合：
- 单插件编程式装载（走 `PluginPath` 请求，不经清单）
- 插件本体的开发（见[插件开发指南](YomkPluginInterface.md)）

> 前置概念：清单实例块中的实例名是**裸名**（同插件内唯一），系统登记后自动拼接为限定名 `插件名.实例名`（见[插件生命周期](PluginLifecycle.md)）。

## 最小可运行示例

以下为 `examples/workflow/manifest.yomk` 原文（双插件系统，随示例程序可构建验证）：

```
#! yomk_plug_sys                                                # 文件标识

# 连接器配置
[ConnectionServiceIns]
lib = ConnectionService/lib/libConnectionService.so
config = ConnectionService/instances/ConnectionService.txt

# 工作空间配置
[WorkspaceServiceIns]
lib = WorkspaceService/lib/libWorkspaceService.so
config = WorkspaceService/instances/WorkspaceService.txt
```

预期行为：`yomk::plug_sys().manifest("<路径>/manifest.yomk").build()` 返回 `BuilderInfo`（`plugins=2`、`instances=2`，`manifest` 为两个实例条目，含限定名/库文件/配置文件）；两实例登记为 `ConnectionService.ConnectionServiceIns`、`WorkspaceService.WorkspaceServiceIns`。

## 语法规则

**首行**必须为格式标识（缺失、不在首行或不匹配则整体解析失败）：

```
#! yomk_plug_sys
```

**实例块**以 `[实例名]` 起块，块内用 `key = value` 提供 `lib` 与 `config` 两个键（顺序任意，各出现一次）：

```
[ConnectionServiceIns]
lib = ConnectionService/lib/libConnectionService.so
config = ConnectionService/instances/ConnectionService.txt
```

| 组成 | 规则 |
|---|---|
| 实例名 | 裸名，同一插件内唯一；空名解析失败；不得含 `.`（限定名分隔符，查找接口按 `插件名.` 前缀匹配）与方括号 |
| `lib` | 相对**清单所在目录**的动态库路径，须含平台相关文件名全名（Linux `libX.so` / macOS `libX.dylib` / Windows `X.dll`）；拒绝绝对路径；每块必填 |
| `config` | 相对**清单所在目录**的实例配置路径，**必填**且文件须存在；拒绝绝对路径；每块必填 |

**严格校验**：块内键仅小写 `lib` / `config`——未知键、重复键、缺少任一键、块外出现键值行、块头缺少 `]` 均解析失败并指明行号（缺键或块内错误定位到块头行号）。

**注释与空行**：`#` 起到行尾均为注释（整行注释与行内注释都支持），空行跳过；首行格式标识之后也可跟行内注释。

## build 流程

解析清单 → 校验实例配置/动态库存在 → 逐块装载（同一 so 路径已加载则复用，不重复 load）→ 按块名创建实例。任一步失败返回 `Failed` 并**指明清单行号**；已成功块不回滚（详见[插件生命周期](PluginLifecycle.md)）。

## 注意事项

- 路径一律相对**清单所在目录**解析，与当前工作目录无关；`<模块>/lib/`、`<模块>/instances/` 仅为示例工程的目录组织约定，Builder 不强制。
- 跨平台部署时各平台使用各自清单文件（库名含平台后缀，清单不代做转换）；目录布局与库名完全解耦。
- 实例配置内容 Builder 不读取，仅把其绝对路径作为 `instance_cfg` 透传给插件工厂（见[插件开发指南](YomkPluginInterface.md)）；但**文件存在性会校验**。
- 同一清单中同一插件装载多条（不同实例）合法：装载幂等，实例各自创建。

## 常见问题 FAQ

- **清单可以用别的文件名吗？** 可以，格式标识只认首行 `#! yomk_plug_sys`；`.yomk` 后缀仅为约定。
- **实例块失败会怎样？** 中止并返回失败原因与行号；之前块已装载的不回滚，修正后重新 `build` 即可。
- **实例配置能省略吗？** 清单里不能：每个实例块都必须有 `lib` 与 `config`，缺 `config` 报 `instance block [名字] missing config`，值为空报 `empty value for 'config'`；文件不存在报 `instance config not found`。直接请求 `/YomkPluginManager/create_instance` 时 `instance_cfg` 可为空（插件工厂收到空串）。
- **键名可以大写或用别名吗？** 不可以：只认小写 `lib` / `config`，未知键直接报错并指明行号。
- **相对路径以哪里为基准？** 只以清单文件所在目录为基准，与 CWD、安装路径均无关。

## 相关跳转

- [门面服务](YomkPlugSysBuilder.md)：`build` 接口的调用方式与 `BuilderInfo` 内省
- [插件生命周期](PluginLifecycle.md)：装载/创建的幂等与失败语义
- [插件开发指南](YomkPluginInterface.md)：清单指向的 so 需满足的契约

## 完整示例

清单 + 两个示例插件模块 + 实例配置的完整示例工程见 `examples/workflow/`（`manifest.yomk` + `ConnectionService/` + `WorkspaceService/`），经 `examples/ExampleYomkPlugSysBuilder.cpp` 一键构建验证。
