# 构建与验证

YomkPlugSys 的编译安装、下游工程 CMake 集成、安装布局与测试体系。依赖 YomkServer 3.6.18 及以上（配置期自动检测，低版本直接报错终止）。

## 适用与不适用

✅ 适合：
- 首次编译安装本扩展
- 下游工程 / 插件工程的 CMake 集成（find_package）
- 运行测试套件与静态检查

❌ 不适合：
- YomkServer 本体的编译安装（见 YomkServer 仓库）
- 运行期接口问题（见[门面服务](YomkPlugSysBuilder.md)）

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装（通过其 `build_ubuntu.sh` 安装后会自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径）

## 编译安装

```bash
source build_ubuntu.sh
```

> 交互式编译：依次询问 YomkServer 安装路径（前置路径）与扩展安装路径，默认均取 `$YOMK_PREFIX_PATH`，可修改。扩展库与 YomkServer 安装到一起（头文件由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供）。

脚本行为：

- 示例程序默认编译并随扩展安装（到 `<安装路径>/bin`），可直接运行 `ExampleYomkPlugSysBuilder` 验证
- 测试程序询问是否编译（直接回车默认编译，输入 n 跳过），仅本地构建不安装（产物在 `test/build/` 下）
- 安装后将扩展 lib 注册到系统动态库搜索路径（复用 `/etc/ld.so.conf.d/yomk.conf`，幂等追加）并刷新 ldconfig 缓存，新开任意终端即可找到扩展 so
- 测试/示例 cmake 带 `-U<NAME>_DIR` 清缓存，强制按本次前缀重新 find_package，避免钉死陈旧安装目录

手动 CMake 等价流程（构建目录可任意）：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="${YOMK_PREFIX_PATH}" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix "${YOMK_PREFIX_PATH}"   # 前缀不可写时加 sudo
```

## 安装布局

| 路径 | 内容 |
|---|---|
| `<prefix>/include/yomk/plug_sys/` | 对外头文件 6 个（引用形态 `<yomk/plug_sys/...>`，与 SDK 各域共用同一 include 根） |
| `<prefix>/lib/libYomkPlugSys.so` | 扩展动态库 |
| `<prefix>/lib/cmake/YomkPlugSys/` | find_package 导出配置 |
| `<prefix>/bin/ExampleYomkPlugSysBuilder` | 入门示例程序 |

版本检测：CMake 配置期要求 `YomkServer >= 3.6.18`（`cmake/YomkServerVersionCheck.cmake`），低版本直接 FATAL_ERROR。

## 下游集成

宿主工程与插件工程统一用 find_package：

```cmake
find_package(YomkPlugSys REQUIRED)
target_link_libraries(demo PRIVATE YomkPlugSys::YomkPlugSys)
```

- 导入 target 已携带 INTERFACE include（`<prefix>/include`），源码直接 `#include <yomk/plug_sys/YomkPluginApi.h>`，无需手动 include_directories
- 插件工程同样链接（SHARED 库引用 ABI 头），示例见[插件开发指南](YomkPluginInterface.md)的 CMake 片段

## 测试体系

测试为独立 CMake 工程（`test/`），不随扩展安装：

```bash
cmake -S test -B test/build -DCMAKE_PREFIX_PATH="${YOMK_PREFIX_PATH}" -DCMAKE_BUILD_TYPE=Release
cmake --build test/build -j

./test/run_tests.sh              # 一键运行全量（默认 <仓库>/test/build）
./test/run_tests.sh --bin DIR    # 指定测试可执行目录
./test/run_tests.sh --timeout N  # 单测试超时秒数（默认 300）
./test/run_tests.sh -h           # 显示用法
```

8 个测试闭环：

| 测试 | 覆盖 |
|---|---|
| TestYomkPluginFacade | 门面链式域入口契约冒烟 |
| TestYomkPluginManifest | 插件系统清单解析（注释/行内注释/绝对路径拒绝/行号报错） |
| TestYomkPluginBuildLifecycle | 构建生命周期端到端 |
| TestYomkPluginBadPlugin | 8 种缺陷插件的失败分支 |
| TestYomkPluginManagerEdge | Manager 边界与内省细节 |
| TestYomkPluginReload | 重复加载/卸载时序与状态机 |
| TestYomkPluginConcurrent | 并发正确性 |
| TestYomkPluginStress | 压力测试 |

运行器行为：失败即停（输出 `[FAIL]` 摘要与日志路径）、日志落盘 `test/test_logs/<时间戳>/`、每个测试独立临时目录运行、超时保护、自动导出 `YOMK_TEST_PLUGIN` 指向同目录 `test_plugin/libTestPlugin.so`。

静态检查（cppcheck + clang-tidy，零告警基线）：

```bash
./test/run_static_checks.sh
```

## 注意事项

- 构建目录可任意（仓库外亦可）；`compile_commands.json` 自动导出到源码根供 clangd 使用。
- 示例程序与示例插件分离安装：`ExampleYomkPlugSysBuilder` 装入 `<prefix>/bin`，但清单路径编译期固化为源码树 `examples/workflow/manifest.yomk`，插件 so 也留在源码树 `examples/workflow/*/lib/`（不安装）——源码树被移动或删除后，已安装的示例程序无法运行。
- 构建产物目录（`build/`、`test/build/`、`examples/build/`）与 `test_logs/` 均被 .gitignore 忽略。
- 交叉编译/换编译器时 ABI 环境指纹（`cmake/AbiInfo.cmake`）会生成 `abi_report.txt`，插件与宿主须同环境编译（见[插件开发指南](YomkPluginInterface.md)）。

## 常见问题 FAQ

- **cmake 报 YomkServer 版本不足？** 安装 YomkServer >= 3.6.18，确认 `CMAKE_PREFIX_PATH` 指向其安装前缀。
- **安装到 /opt/yomk 提示无权限？** 脚本自动加 sudo；手动流程给 `cmake --install` 自行加 sudo。
- **下游链接找不到 libYomkPlugSys.so？** 确认 ldconfig 已刷新（脚本自动执行）；或运行时设 `LD_LIBRARY_PATH=<prefix>/lib`。

## 相关跳转

- [快速开始](quickstart.md)：从安装到跑通的最短路径
- [YomkServer 文档](../../YomkServer/docs/README.md)：框架本体（服务总线/消息包/日志）

## 完整示例

`build_ubuntu.sh` 全流程输出示例与 `test/` 各测试的断言细节见源码；安装后直接运行 `<prefix>/bin/ExampleYomkPlugSysBuilder` 观察端到端结果。
