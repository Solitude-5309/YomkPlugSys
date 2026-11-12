/*
 * ExampleYomkPluginSystemBuilder：YomkPluginSystem 入门示例
 *
 * 面向首次使用的用户，用 examples/workflow 的两个示例插件串起最常用接口：
 *   初始化 -> 构建插件工程（含构建信息）-> 内省（插件/实例/按插件过滤/按实例查插件/实例文件）-> 卸载
 *
 * 每一步注释标明【接口 / 输入 / 输出】，运行即可观察实际结果：
 *   <安装路径>/bin/ExampleYomkPluginSystemBuilder
 * 清单路径由 CMake 编译期注入（指向 examples/workflow/manifest.yomk）。
 */
#include <yomk/plugin_system/YomkPluginApi.h>
#include <yomk/server/YomkServerApi.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace yomk;

namespace
{
bool isOk(const YomkResponse& r)
{
    return r.status == ResStatus::Succeeded;
}

/* 打印分节标题 */
void section(const std::string& title)
{
    std::cout << "\n========== " << title << " ==========" << std::endl;
}

/* 请求失败统一提示并退出（入门示例遇错即停，便于定位问题） */
void fail(const std::string& what, const std::string& reason)
{
    std::cout << "[" << what << "] 失败: " << reason << std::endl;
    std::exit(1);
}
}  // namespace

int main()
{
    /* ---------- 0. 初始化 ----------
     * 接口：YOMK_INIT() + YOMK_NEW_SERVICE(服务类)
     * 说明：只注册门面服务 YomkPluginSystemBuilder，机制层 Loader /
     *       数据层 Manager 由其 init() 内部自动注册，用户无感知 */
    YOMK_INIT();
    YOMK_NEW_SERVICE(YomkPluginSystemBuilder);

    const std::string manifest = WORKFLOW_MANIFEST_PATH;

    /* ---------- 1. 查询扩展版本 ----------
     * 接口：YOMKPLUGIN_VERSION()
     * 输入：无
     * 输出：无返回值，宏内部自动打印版本描述 */
    section("1. 查询扩展版本 YOMKPLUGIN_VERSION()");
    YOMKPLUGIN_VERSION();

    /* ---------- 2. 构建插件工程并查看构建信息 ----------
     * 接口：YOMKPLUGIN_BUILD(workflowPath)，构建后紧接 YOMKPLUGIN_INFO_BUILDER()
     *       打印同一次构建的结构化信息（BUILD 返回的 String 汇总与构建日志一致，
     *       不再单独打印）
     * 输入：清单文件路径。清单格式：首行 #! yomk_plugin_system，
     *       之后每行一个条目：实例名@动态库相对路径@实例文件相对路径
     * 输出：BuilderInfo（最近构建：清单条目 manifest / 路径 path / 结果 result /
     *       插件数 plugins / 实例数 instances）
     * 副作用：逐条目加载插件 so 并创建实例（实例名取自清单条目） */
    section("2. 构建插件工程 YOMKPLUGIN_BUILD(manifest)");
    std::cout << "清单: " << manifest << std::endl;
    YomkResponse resp = YOMKPLUGIN_BUILD(manifest);
    if (!isOk(resp))
    {
        fail("构建", resp.msg);
    }
    resp = YOMKPLUGIN_INFO_BUILDER();
    if (!isOk(resp))
    {
        fail("查询构建信息", resp.msg);
    }
    YomkUnPackPkg(resp.data, BuilderInfo, builder);
    if (!builder)
    {
        fail("查询构建信息", "响应数据缺失");
    }
    std::cout << "builder" << std::endl;
    std::cout << "  manifest:" << std::endl;
    for (const auto& line : builder->d.manifest)
    {
        std::cout << "    " << line << std::endl;
    }
    std::cout << "  path: " << builder->d.path << std::endl;
    std::cout << "  result:" << builder->d.result << std::endl;
    std::cout << "  plugins:" << builder->d.plugins << std::endl;
    std::cout << "  instances:" << builder->d.instances << std::endl;

    /* ---------- 3. 查询插件列表 ----------
     * 接口：YOMKPLUGIN_INFO_PLUGINS()
     * 输入：无
     * 输出：PluginMetaArray（插件元数据，来自插件导出的 yomk_plugin_meta） */
    section("3. 插件列表 YOMKPLUGIN_INFO_PLUGINS()");
    resp = YOMKPLUGIN_INFO_PLUGINS();
    if (!isOk(resp))
    {
        fail("查询插件", resp.msg);
    }
    YomkUnPackPkg(resp.data, PluginMetaArray, metas);
    if (!metas)
    {
        fail("查询插件", "响应数据缺失");
    }
    for (const auto& m : metas->d)
    {
        std::cout << m.name << std::endl;
        std::cout << "  abi_version: " << m.abi_version << std::endl;
        std::cout << "  type: " << m.type << std::endl;
        std::cout << "  version: " << m.version << std::endl;
        std::cout << "  author: " << m.author << std::endl;
        std::cout << "  path: " << m.path << std::endl;
        std::cout << "  description: " << m.description << std::endl;
    }

    /* ---------- 4. 查询实例列表 ----------
     * 接口：YOMKPLUGIN_INFO_INSTANCES()
     * 输入：无
     * 输出：InstanceInfoArray（全量实例明细） */
    section("4. 实例列表 YOMKPLUGIN_INFO_INSTANCES()");
    resp = YOMKPLUGIN_INFO_INSTANCES();
    if (!isOk(resp))
    {
        fail("查询实例", resp.msg);
    }
    YomkUnPackPkg(resp.data, InstanceInfoArray, instances);
    if (!instances)
    {
        fail("查询实例", "响应数据缺失");
    }
    for (const auto& i : instances->d)
    {
        std::cout << i.instanceName << " --> " << i.pluginName << std::endl;
    }

    /* ---------- 5. 按插件过滤实例 ----------
     * 接口：YOMKPLUGIN_INFO_INSTANCES_BY(pluginName)
     * 输入：pluginName（未知插件返回 eNo 并携带原因）
     * 输出：InstanceInfoArray（仅该插件下的实例） */
    section("5. 按插件过滤实例 YOMKPLUGIN_INFO_INSTANCES_BY(\"ConnectionService\")");
    resp = YOMKPLUGIN_INFO_INSTANCES_BY("ConnectionService");
    if (!isOk(resp))
    {
        fail("过滤实例", resp.msg);
    }
    YomkUnPackPkg(resp.data, InstanceInfoArray, filtered);
    if (!filtered)
    {
        fail("过滤实例", "响应数据缺失");
    }
    for (const auto& i : filtered->d)
    {
        std::cout << i.instanceName << " --> " << i.pluginName << std::endl;
    }

    /* ---------- 6. 按实例查所属插件 ----------
     * 接口：YOMKPLUGIN_INFO_PLUGIN_BY(instanceName)
     * 输入：instanceName（限定名 插件名.实例名，取步骤 4 列出的任一实例名，未知实例返回 eNo 并携带原因）
     * 输出：PluginMeta（该实例所属插件的元数据） */
    section("6. 按实例查插件 YOMKPLUGIN_INFO_PLUGIN_BY(\"ConnectionService.ConnectionServiceIns\")");
    resp = YOMKPLUGIN_INFO_PLUGIN_BY("ConnectionService.ConnectionServiceIns");
    if (!isOk(resp))
    {
        fail("按实例查插件", resp.msg);
    }
    YomkUnPackPkg(resp.data, PluginMeta, instMeta);
    if (!instMeta)
    {
        fail("按实例查插件", "响应数据缺失");
    }
    std::cout << instMeta->d.name << std::endl;
    std::cout << "  abi_version: " << instMeta->d.abi_version << std::endl;
    std::cout << "  type: " << instMeta->d.type << std::endl;
    std::cout << "  version: " << instMeta->d.version << std::endl;
    std::cout << "  author: " << instMeta->d.author << std::endl;
    std::cout << "  path: " << instMeta->d.path << std::endl;
    std::cout << "  description: " << instMeta->d.description << std::endl;

    /* ---------- 7. 查看实例文件内容 ----------
     * 接口：YOMKPLUGIN_INFO_INSTANCES_FILE(instanceName)
     * 输入：instanceName（限定名 插件名.实例名，未知实例返回 eNo 并携带原因）
     * 输出：String（该实例创建时透传的实例文件内容） */
    section("7. 查看实例文件 YOMKPLUGIN_INFO_INSTANCES_FILE(\"ConnectionService.ConnectionServiceIns\")");
    resp = YOMKPLUGIN_INFO_INSTANCES_FILE("ConnectionService.ConnectionServiceIns");
    if (!isOk(resp))
    {
        fail("查看实例文件", resp.msg);
    }
    YomkUnPackPkg(resp.data, String, fileContent);
    if (!fileContent)
    {
        fail("查看实例文件", "响应数据缺失");
    }
    std::cout << fileContent->d << std::endl;

    /* ---------- 8. 卸载插件（两种语义对比） ----------
     * 接口：YOMKPLUGIN_TRY_UNLOAD(pluginName)  尝试卸载：有存活实例则拒绝（引用保护）
     * 接口：YOMKPLUGIN_UNLOAD(pluginName)      强制卸载：先销毁全部实例再卸载
     * 输入：pluginName
     * 输出：String(ok/msg) */
    section("8. 卸载插件（TRY_UNLOAD 引用保护 vs UNLOAD 强制）");
    resp = YOMKPLUGIN_TRY_UNLOAD("ConnectionService");
    std::cout << "TRY_UNLOAD（实例存活中）: " << (isOk(resp) ? "ok" : resp.msg) << std::endl;
    resp = YOMKPLUGIN_UNLOAD("ConnectionService");
    if (!isOk(resp))
    {
        fail("卸载 ConnectionService", resp.msg);
    }
    std::cout << "UNLOAD ConnectionService: ok" << std::endl;
    resp = YOMKPLUGIN_UNLOAD("WorkspaceService");
    if (!isOk(resp))
    {
        fail("卸载 WorkspaceService", resp.msg);
    }
    std::cout << "UNLOAD WorkspaceService: ok" << std::endl;

    section("示例结束");
    std::cout << "全部接口演示完毕，系统已复位（0 插件 0 实例）。" << std::endl;
    return 0;
}
