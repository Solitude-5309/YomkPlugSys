# 插件生命周期

插件从 so 装载、实例创建、销毁到卸载的完整状态机语义：哪一步幂等、哪一步拒绝、失败时返回什么。理解本章即可正确编排 生命周期接口的调用 顺序与错误处理；接口的调用方式（链式域入口）见[门面服务](YomkPluginSystemBuilder.md)。

## 适用与不适用

✅ 适合：
- 编排插件装载/卸载前预判各接口的成败边界
- 处理"实例还活着能否卸载""重复构建同一清单"等状态问题
- 白盒测试或扩展维护者理解内部两级服务的协作

❌ 不适合：
- 查接口怎么调（见[门面服务](YomkPluginSystemBuilder.md)）
- 编写插件本体（见[插件开发指南](YomkPluginInterface.md)）

> 前置概念：限定名 = `插件名.实例名`（全局唯一主键）；插件名即插件 meta 的 `name` 字段，由 so 的 `yomk_plugin_meta()` 导出（见[插件开发指南](YomkPluginInterface.md)）。

## 状态机总览

```
            load / build                     create_instance
 so 文件 ─────────────────→ 已加载（无实例） ──────────────→ 存在存活实例
                              ↑   │                            │      │
                     load 幂等│   │ try_unload                 │      │ force_unload /
                     跳过     │   │ （无存活实例时成功）         │      │ destroy_instance(逐个)
                              │   ↓                            │      ↓
                              │ 已卸载 ←────────────────────────────── 无存活实例
                              └────────────（可重新 load）
```

- **已加载**：so 已 dlopen 且 meta 已登记；随时可创建实例
- **存活实例**：实例表中被 shared_ptr 持有的实例；宿主侧（消息包内）的引用同样计入存活判定
- **卸载**：dlclose 句柄并移除登记；之后同名重新 `load` 等同于全新加载

## 各阶段语义

### 装载（load / build）

- `load`：dlopen + 符号解析（`yomk_plugin_meta` / `yomk_plugin_create_instance` / `yomk_plugin_delete_instance`）+ ABI 版本比对 + 同名 meta 拒绝；返回插件名（= meta.name）
- **已加载的插件重复 load 幂等跳过**（返回成功，不重复登记）
- ABI 版本不符 / 符号缺失 / meta 非法（空名、空实例名等）→ `Failed`，bad 插件不进入插件表
- `build` = 清单解析 + 逐条 load + create_instance 的编排；任一条目失败即中止返回 `Failed` 并指明清单行号，**已成功的条目不回滚**

### 实例创建（create_instance / build）

- 实例名（裸名）由调用方指定，**同一插件内唯一**；Manager 拼接插件名为全局唯一限定名 `插件名.实例名`
- 同插件内重名 → `Failed`；不同插件允许同名（限定名不同）
- 实例由 `yomk_plugin_create_instance` 工厂函数创建：返回 `nullptr`（含工厂内部异常被捕获）→ `Failed`
- `instanceFile`（实例文件绝对路径）原样透传插件工厂，插件系统不读不解析

### 实例销毁（destroy_instance / force_unload 内部步骤）

- 销毁即调用 `yomk_plugin_delete_instance` 并从实例表移除，限定名立即可复用
- 宿主侧若仍有消息包持有该实例（`PluginInstance` shared_ptr），实例对象本身存活至最后引用释放；但其登记已删除，内省不再可见

### 卸载（unload / try_unload）

| 接口 | 有存活实例 | 无存活实例 |
|---|---|---|
| `tryUnload(pluginName)` | `Failed`（插件保持加载） | 卸载成功 |
| `unload(pluginName)` | **先删除该插件全部实例**，再卸载 | 卸载成功 |

> Warning：`unload` 是破坏性操作——存活实例被强制销毁，宿主侧 shared_ptr 随即悬空（指向已析构对象）；仅当确认无人再持有时使用。

## 内部两级服务（进阶）

门面服务内部托管两个纯请求协作的服务，其 URL 仅扩展内部与白盒测试直调，域入口不暴露：

| 服务 | 层 | 职责 |
|---|---|---|
| `/YomkPluginLoader` | 机制层 | dlopen/dlsym/dlclose、meta 读取、实例创建机制；只持句柄表与 weak_ptr 存活表，**不登记插件、不拥有实例** |
| `/YomkPluginManager` | 数据层 | 插件表（meta 深拷贝）+ 实例表（shared_ptr 唯一所有者）、限定名管理、生命周期编排 |

- 门面 `build/unload/...` 全部经请求代理到 Manager 完成，自身不持有插件/实例
- Loader 的存活表用于卸载引用保护：有存活实例时 `unload` 拒绝（引用计数语义）
- 门面服务 init 时注册二者、deinit 逆序注销；未注册门面则内部服务也不存在

## 注意事项

- 所有生命周期接口为**同步阻塞**调用；无异步版本。
- 实例与插件状态仅存于进程内存：进程退出即全部卸载，无持久化。
- `build` 中途失败的清单**不回滚**已成功条目；重复执行同一清单时已加载插件幂等跳过，仅重名实例会失败——修正清单后重新 `build` 即可收敛。
- 宿主持有 `PluginInstance`（shared_ptr）期间，对应插件无法被 `tryUnload`（引用保护）；跨请求传递实例时注意归还时机。

## 常见问题 FAQ

- **重复 build 同一清单会怎样？** 已加载插件幂等跳过；实例重名 `Failed` 指明行号；新条目正常装载。
- **实例被销毁后宿主还能用吗？** 不能：`unload` 强制销毁后宿主侧 shared_ptr 悬空，解引用为 UB；`destroy_instance` 路径宿主侧引用计数保护对象本体，但登记已删除。
- **同名插件（不同 so）能同时加载吗？** 不能：同 meta.name 重复 load 拒绝（重名校验）；需加载同名新版本请先卸载旧插件。
- **实例名裸名全局重复？** 允许：限定名才是全局唯一主键，不同插件可有同名实例。

## 相关跳转

- [门面服务](YomkPluginSystemBuilder.md)：生命周期接口的调用方式与返回解包
- [插件开发指南](YomkPluginInterface.md)：状态机中各导出函数何时被调用

## 完整示例

生命周期全分支（含重名拒绝、卸载引用保护、try/force 对比）的覆盖见 `test/`（8 个测试闭环，运行方式见[构建与验证](BuildCmake.md)）。
