# 插件生命周期

插件从 so 装载、实例创建、销毁到卸载的完整状态机语义：哪一步幂等、哪一步拒绝、失败时返回什么。理解本章即可正确编排生命周期接口的调用顺序与错误处理；接口的调用方式（链式域入口）见[门面服务](YomkPluginSystemBuilder.md)。

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
                    build 复用│   │ try_unload                 │      │ force_unload /
                    （已加载）│   │ （无存活实例时成功）         │      │ destroy_instance(逐个)
                              │   ↓                            │      ↓
                              │ 已卸载 ←────────────────────────────── 无存活实例
                              └────────────（可重新 load）
```

- **已加载**：so 已 dlopen 且 meta 已登记；随时可创建实例
- **存活实例**：实例表中被 shared_ptr 持有的实例；宿主侧（消息包内）的引用同样计入存活判定
- **卸载**：dlclose 句柄并移除登记；之后同名重新 `load` 等同于全新加载

## 各阶段语义

### 装载（load / build）

- `load`：dlopen + 符号解析（`yomk_plugin_meta` / `yomk_plugin_create_instance` / `yomk_plugin_delete_instance`）+ ABI 版本比对 + 同名 meta 拒绝；Manager 登记时再拒绝含 `.` 的插件名；返回插件名（= meta.name）
- **build 复用已加载插件**：清单条目的 so 绝对路径已在插件表内则跳过 load、直接复用插件名（不重复登记）；直接 `load` 同 meta 名的插件则返回 `Failed`（`plugin already loaded`）
- ABI 版本不符 / 符号缺失 / meta 为空或 `name` 为空 / 插件名含 `.` / 与已加载插件重名 → `Failed`，插件不进入插件表
- `build` = 清单解析 + 逐条 load + create_instance 的编排；任一条目失败即中止返回 `Failed` 并指明清单行号，**已成功的条目不回滚**

### 实例创建（create_instance / build）

- 实例名（裸名）由调用方指定，**同一插件内唯一**；Manager 拼接插件名为全局唯一限定名 `插件名.实例名`
- 同插件内重名 → `Failed`；不同插件允许同名（限定名不同）
- 实例由 `yomk_plugin_create_instance` 工厂函数创建：返回 `nullptr`（含工厂内部异常被捕获）→ `Failed`
- `instanceCfg`（实例配置绝对路径）原样透传插件工厂：框架不解析内容，但会记录路径供 `inst_cfg` 回读；清单构建时还会预校验文件存在

### 实例销毁（destroy_instance / force_unload 内部步骤）

- 销毁即移出实例表（限定名立即可复用），并在最后一个引用释放时调用 `yomk_plugin_delete_instance`
- 宿主侧若仍有消息包持有该实例（`PluginInstance` shared_ptr），实例对象本身存活至最后引用释放；但其登记已删除，内省不再可见

### 卸载（unload / try_unload）

| 接口 | 有存活实例 | 无存活实例 |
|---|---|---|
| `plg_name(n).try_unload()` | `Failed`（插件保持加载，实例不受影响） | 卸载成功 |
| `plg_name(n).unload()` | 先清空该插件的实例表再卸载；实例引用全部释放后成功 | 卸载成功 |

> `unload` 的"强制"仅指清空 Manager 实例表（不再等待实例自然销毁），**不夺走宿主已持有的引用**：Loader 卸载前仍按存活表统一判活，只要还有引用存在就返回 `Failed`（`instances still alive`）。此时插件保持已加载、实例表已被清空（半完成态），须释放全部引用后重新 `unload`，或重新 `build`/`create` 恢复实例。因为 dlclose 不会在有存活引用时发生，宿主持有的 shared_ptr 不会悬空——引用保护是双向的。

## 内部两级服务（进阶）

门面服务内部托管两个纯请求协作的服务，其 URL 仅扩展内部与白盒测试直调，域入口不暴露：

| 服务 | 层 | 职责 |
|---|---|---|
| `/YomkPluginLoader` | 机制层 | dlopen/dlsym/dlclose、meta 读取、实例创建机制；只持句柄表与 weak_ptr 存活表，**不登记插件、不拥有实例** |
| `/YomkPluginManager` | 数据层 | 插件表（meta 深拷贝）+ 实例表（shared_ptr 唯一所有者）、限定名管理、生命周期编排 |

- 门面 `build/unload/...` 全部经请求代理到 Manager 完成，自身不持有插件/实例
- 判活只有一处：Loader 的 weak 存活表。`try_unload` 直接委托它；`unload` 先清 Manager 实例表、再委托同一判活，因此外部仍有引用时两者都会被拒绝
- 门面服务 init 时注册二者、deinit 逆序注销；未注册门面则内部服务也不存在

## 注意事项

- 所有生命周期接口为**同步阻塞**调用；无异步版本。
- 实例与插件状态仅存于进程内存：进程退出即全部卸载，无持久化。
- `build` 中途失败的清单**不回滚**已成功条目；重复执行同一清单时已加载插件幂等跳过，仅重名实例会失败——修正清单后重新 `build` 即可收敛。
- 宿主持有 `PluginInstance`（shared_ptr，含响应包 `data` 内的一份）期间，对应插件既不能被 `try_unload` 也不能被 `unload`（Loader 判活拒绝）；跨请求传递实例时注意归还时机。

## 常见问题 FAQ

- **重复 build 同一清单会怎样？** 已加载插件幂等跳过；实例重名 `Failed` 指明行号；新条目正常装载。
- **实例被销毁后宿主还能用吗？** 分两种：`destroy_instance` 只删登记，宿主 shared_ptr 仍有效（最后一个引用释放时才真正析构）；`unload` 不会在有引用时 dlclose，故不存在悬空——但 `unload` 失败于判活时实例表已被清空，需重新 `build`/`create`。
- **同名插件（不同 so）能同时加载吗？** 不能：同 meta.name 重复 load 拒绝（重名校验）；需加载同名新版本请先卸载旧插件。
- **实例名裸名全局重复？** 允许：限定名才是全局唯一主键，不同插件可有同名实例。

## 相关跳转

- [门面服务](YomkPluginSystemBuilder.md)：生命周期接口的调用方式与返回解包
- [插件开发指南](YomkPluginInterface.md)：状态机中各导出函数何时被调用

## 完整示例

生命周期全分支（含重名拒绝、卸载引用保护、try/force 对比）的覆盖见 `test/`（8 个测试闭环，运行方式见[构建与验证](BuildCmake.md)）。
