// YomkPlugSys 入门示例：初始化 -> 构建插件系统 -> 内省 -> 卸载，串起最常用接口。
// 每节标注【接口/输入/输出】；清单路径由 CMake 编译期注入（examples/workflow/manifest.yomk）。
#include <yomk/plug_sys/YomkPlugSysApi.h>
#include <yomk/server/YomkServerApi.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace yomk;

namespace
{
bool isOk(const yomk::payload::Response& r)
{
    return r.status == yomk::payload::ResStatus::Succeeded;
}

// 打印分节标题
void section(const std::string& title)
{
    std::cout << "\n========== " << title << " ==========" << std::endl;
}

// 请求失败统一提示并退出：入门示例遇错即停，便于定位问题
void fail(const std::string& what, const std::string& reason)
{
    std::cout << "[" << what << "] 失败: " << reason << std::endl;
    std::exit(1);
}
}  // namespace

int main()
{
    // 0. 初始化
    // 接口：yomk::server().init() + yomk::server().srv<服务类>().create()
    // 说明：只注册门面服务 YomkPlugSysBuilder，机制层 Loader 与数据层 Manager 由其 init() 内部自动注册
    yomk::server().init();
    yomk::server().srv<YomkPlugSysBuilder>().create();

    const std::string manifest = WORKFLOW_MANIFEST_PATH;

    // 1. 查询扩展版本
    // 接口：yomk::plugin().version()
    // 输入：无
    // 输出：payload::Response，解包 String 后打印版本描述
    section("1. 查询扩展版本 yomk::plugin().version()");
    yomk::payload::Response verResp = yomk::plugin().version();
    auto ver = yomk::msgs::unpack<yomk::msgs::String>(verResp.data);
    if (ver)
    {
        yomk::log().tag("YomkPlugSys").info(ver->d);
    }

    // 2. 构建插件系统并查看构建信息
    // 接口：yomk::plugin().manifest(manifestPath).build() + yomk::plugin().builder()
    // 输入：清单文件路径。首行 #! yomk_plug_sys，之后每行一个条目：
    //       实例名@动态库相对路径@实例配置相对路径
    // 输出：BuilderInfo（最近构建的清单条目 manifest / 路径 path / 结果 result / 插件数 plugins / 实例数 instances）
    // 副作用：逐条目加载插件 so 并创建实例，实例名取自清单条目
    section("2. 构建插件系统 yomk::plugin().manifest(manifest).build()");
    std::cout << "清单: " << manifest << std::endl;
    yomk::payload::Response resp = yomk::plugin().manifest(manifest).build();
    if (!isOk(resp))
    {
        fail("构建", resp.msg);
    }
    resp = yomk::plugin().builder();
    if (!isOk(resp))
    {
        fail("查询构建信息", resp.msg);
    }
    auto builder = yomk::msgs::unpack<yomk::msgs::BuilderInfo>(resp.data);
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

    // 3. 查询插件列表
    // 接口：yomk::plugin().plugins()
    // 输入：无
    // 输出：PluginMetaArray，元数据来自插件导出的 yomk_plugin_meta
    section("3. 插件列表 yomk::plugin().plugins()");
    resp = yomk::plugin().plugins();
    if (!isOk(resp))
    {
        fail("查询插件", resp.msg);
    }
    auto metas = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
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

    // 4. 查询实例列表
    // 接口：yomk::plugin().instances()
    // 输入：无
    // 输出：InstanceInfoArray（全量实例明细）
    section("4. 实例列表 yomk::plugin().instances()");
    resp = yomk::plugin().instances();
    if (!isOk(resp))
    {
        fail("查询实例", resp.msg);
    }
    auto instances = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
    if (!instances)
    {
        fail("查询实例", "响应数据缺失");
    }
    for (const auto& i : instances->d)
    {
        std::cout << i.instanceName << " --> " << i.pluginName << std::endl;
    }

    // 5. 按插件过滤实例
    // 接口：yomk::plugin().plg_name(pluginName).instances()
    // 输入：pluginName（未知插件返回失败，原因见 resp.msg）
    // 输出：InstanceInfoArray（仅该插件下的实例）
    section("5. 按插件过滤实例 yomk::plugin().plg_name(\"ConnectionService\").instances()");
    resp = yomk::plugin().plg_name("ConnectionService").instances();
    if (!isOk(resp))
    {
        fail("过滤实例", resp.msg);
    }
    auto filtered = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
    if (!filtered)
    {
        fail("过滤实例", "响应数据缺失");
    }
    for (const auto& i : filtered->d)
    {
        std::cout << i.instanceName << " --> " << i.pluginName << std::endl;
    }

    // 6. 按实例查所属插件
    // 接口：yomk::plugin().inst_name(instanceName).get_plg_info()
    // 输入：instanceName 为限定名 插件名.实例名，取步骤 4 列出的任一实例名；未知实例返回失败，原因见 resp.msg
    // 输出：PluginMeta（该实例所属插件的元数据）
    section("6. 按实例查插件 yomk::plugin().inst_name(\"ConnectionService.ConnectionServiceIns\").get_plg_info()");
    resp = yomk::plugin().inst_name("ConnectionService.ConnectionServiceIns").get_plg_info();
    if (!isOk(resp))
    {
        fail("按实例查插件", resp.msg);
    }
    auto instMeta = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
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

    // 7. 查看实例配置内容
    // 接口：yomk::plugin().inst_cfg(instanceName)
    // 输入：instanceName 为限定名 插件名.实例名；未知实例返回失败，原因见 resp.msg
    // 输出：String（该实例创建时透传的实例配置内容）
    section("7. 查看实例配置 yomk::plugin().inst_cfg(\"ConnectionService.ConnectionServiceIns\")");
    resp = yomk::plugin().inst_cfg("ConnectionService.ConnectionServiceIns");
    if (!isOk(resp))
    {
        fail("查看实例配置", resp.msg);
    }
    auto fileContent = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
    if (!fileContent)
    {
        fail("查看实例配置", "响应数据缺失");
    }
    std::cout << fileContent->d << std::endl;

    // 8. 卸载插件
    // 接口：yomk::plugin().plg_name(pluginName).try_unload() 尝试卸载：有存活实例则拒绝
    //       yomk::plugin().plg_name(pluginName).unload()    强制卸载：先清空实例表再卸载（外部仍有实例引用时同样拒绝）
    // 输入：pluginName；输出：String(ok/msg)
    section("8. 卸载插件（TRY_UNLOAD 引用保护 vs UNLOAD 强制）");
    resp = yomk::plugin().plg_name("ConnectionService").try_unload();
    std::cout << "TRY_UNLOAD（实例存活中）: " << (isOk(resp) ? "ok" : resp.msg) << std::endl;
    resp = yomk::plugin().plg_name("ConnectionService").unload();
    if (!isOk(resp))
    {
        fail("卸载 ConnectionService", resp.msg);
    }
    std::cout << "UNLOAD ConnectionService: ok" << std::endl;
    resp = yomk::plugin().plg_name("WorkspaceService").unload();
    if (!isOk(resp))
    {
        fail("卸载 WorkspaceService", resp.msg);
    }
    std::cout << "UNLOAD WorkspaceService: ok" << std::endl;

    section("示例结束");
    std::cout << "全部接口演示完毕，系统已复位（0 插件 0 实例）。" << std::endl;
    return 0;
}
