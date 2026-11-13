# 插件开发指南（YomkPluginInterface）

面向插件开发者：编写一个可被 YomkPlugSys 装载的 so 插件，只需实现 ABI 契约三件套——`YomkPluginMeta` 元数据 C 结构体、`YomkPluginInterface` 实例抽象接口、三个 `extern "C"` 导出符号（或经 `YOMKPLUGIN_EXPORT` 一键导出）。无需了解框架服务细节。插件侧最小依赖为 `<yomk/plug_sys/YomkPluginInterface.h>`（下文示例用聚合头 `YomkPluginApi.h`，便于一处引入）。

## 适用与不适用

✅ 适合：
- 开发新的 so 插件（首次编写或对照契约复查）
- 理解 `instance_name` / `instance_cfg` 两个工厂参数的语义
- 排查"插件加载失败 / 创建实例失败"的契约原因

❌ 不适合：
- 宿主侧调用接口（见[门面服务](YomkPlugSysBuilder.md)）
- 清单文件的编写（见[清单格式](PluginManifest.md)）

> 前置概念：插件即 so 库；实例即插件运行期对象，名字为限定名 `插件名.实例名`（见[插件生命周期](PluginLifecycle.md)）。

## 最小可运行插件

完整可编译，复制保存为 `MyPlugin.cpp` 即可：

```cpp
#include <yomk/plug_sys/YomkPluginApi.h>
#include <string>

class MyInstance : public YomkPluginInterface
{
public:
    MyInstance(const std::string& name) : m_name(name) {}

    virtual const char* instanceName() const override { return m_name.c_str(); }
    virtual const char* instanceType() const override { return "demo"; }

private:
    std::string m_name;   // 返回的 const char* 必须在实例存活期内有效
};

static const YomkPluginMeta g_meta = {
    YOMKPLUGIN_ABI_VERSION, "MyPlugin", "demo", "0.0.1", "author", "description"
};

static const YomkPluginMeta* metaFn() { return &g_meta; }

static YomkPluginInterface* createFn(const char* instance_name, const char* instance_cfg)
{
    if (!instance_name || !*instance_name) return nullptr;   // 实例名必填，空名视为创建失败
    try { return new MyInstance(instance_name); }
    catch (...) { return nullptr; }   // 导出函数必须捕获异常，失败返回 nullptr
}

static void deleteFn(YomkPluginInterface* instance) { delete instance; }

YOMKPLUGIN_EXPORT(metaFn, createFn, deleteFn)
```

编译为 SHARED 库（`YomkServer::YomkServer` 提供 `<yomk/plug_sys/...>` 所在的 include 根与 SDK 编译宏）：

```cmake
find_package(YomkServer REQUIRED)

add_library(MyPlugin SHARED MyPlugin.cpp)
target_link_libraries(MyPlugin PRIVATE YomkServer::YomkServer)
```

预期行为：`yomk::plugin().manifest(...).build()` 装载后，`plugins()` 可见 `MyPlugin v0.0.1`；创建的实例名为宿主指定的限定名。

## 契约三件套

### YomkPluginMeta（元数据 C 结构体）

| 字段 | 含义 |
|---|---|
| `abi_version` | 固定填 `YOMKPLUGIN_ABI_VERSION`（独立常量，不随扩展版本变化）；宿主装载时比对，不符拒绝 |
| `name` | 插件名（装载主键；同进程内重复即拒绝，见[生命周期](PluginLifecycle.md)） |
| `type` | 插件类型描述（自由文本，供内省展示） |
| `version` | 插件版本（自由文本） |
| `author` / `description` | 内省展示用 |

meta 必须为静态存储（导出函数返回稳定指针，装载后宿主深拷贝登记）。

### YomkPluginInterface（实例抽象接口）

| 方法 | 契约 |
|---|---|
| `instanceName()` | 返回实例限定名 `插件名.实例名`——**须以工厂传入的 instance_name 为准**；返回指针在实例存活期内有效 |
| `instanceType()` | 实例类型描述（自由文本） |
| `userData` | 实例附带数据指针，宿主不读不释，供宿主侧扩展使用 |

### 三个 extern "C" 导出符号

```cpp
const YomkPluginMeta* yomk_plugin_meta();                     // 返回静态常量指针
YomkPluginInterface* yomk_plugin_create_instance(const char* instance_name, const char* instance_cfg);
void yomk_plugin_delete_instance(YomkPluginInterface* instance);   // 内部 delete
```

`YOMKPLUGIN_EXPORT(metaFn, createFn, deleteFn)` 宏一键完成三个符号的导出声明，等价于手写 `extern "C"`。

## 工厂参数语义

| 参数 | 语义 |
|---|---|
| `instance_name` | 宿主指定的实例限定名 `插件名.实例名`（全局唯一）；插件**必须**以传入名称作为实例名（`instanceName()` 返回它） |
| `instance_cfg` | 透传参数：创建时指定的实例配置**绝对路径**；框架不读不解析，是否使用/如何解析由插件实现自行决定（直接请求时可为空，清单构建时必填且文件须存在）。内容可经内省接口按限定名回读 |

## 注意事项

- 插件与宿主须同编译器、共用本扩展安装的头文件（ABI 依赖 vtable 布局）；ABI 版本由 `YOMKPLUGIN_ABI_VERSION` 把关，不符即拒绝装载。
- 导出函数**不得让异常逃逸**：`create_instance` 内部须 catch 全部异常并返回 `nullptr`（宿主按创建失败处理）。
- 不得在 `yomk_plugin_meta()` 中做耗时/有副作用操作：装载期实时调用；Loader 的 `meta` 接口每次请求都重新调用导出函数，不做缓存。
- so 文件名须含平台相关全名（Linux `libX.so` / macOS `libX.dylib`），由清单或 `PluginPath` 指定（见[清单格式](PluginManifest.md)）。

## 常见问题 FAQ

- **加载失败的可能原因？** 符号缺失（未导出三件套）/ ABI 版本不符 / meta 为空或 `name` 为空 / 插件名含 `.` / 与已加载插件同名 / so 路径不存在——均返回 `Failed` 并带原因。
- **一个 so 能出多个实例吗？** 能：工厂函数按调用反复创建；实例间共享插件级静态数据，须自行保证线程安全。
- **instance_cfg 必须存在吗？** 只有清单构建会校验文件存在；直接 `create_instance` 请求不校验。内容一律不解析，读取行为由插件决定。
- **插件能自己调插件系统的接口吗？** 能：插件作为进程内代码可直接用 `yomk::plugin()` 域入口（注意避免在构造/destruct 中递归创建同插件实例导致死锁）。

## 相关跳转

- [插件生命周期](PluginLifecycle.md)：导出函数何时被调用、实例何时销毁
- [清单格式](PluginManifest.md)：so 路径与实例配置如何随清单指定
- [门面服务](YomkPlugSysBuilder.md)：装载后的内省与卸载

## 完整示例

契约完整覆盖（含第二插件跨插件同名实例用例）见 `test/test_plugin/TestPlugin.cpp` 与 `test/test_plugin/SecondPlugin.cpp`；业务侧真实用法见 `examples/workflow/`（两个示例插件模块）。
