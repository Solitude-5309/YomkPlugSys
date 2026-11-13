// TestYomkPluginBuildLifecycle：构建生命周期端到端测试（闭环3）
// 覆盖：合法构建与内省基线（实例元数据/instance_file 透传观测/插件 meta）、
// so 复用幂等与实例重名、destroy 重建（白盒 URL）、try_unload 拒绝与放行、
// unload 强卸含存活实例、卸载后重载、缺文件行号错误。
// 夹具模式同 TestYomkPluginManifest：<测试可执行目录>/manifest，与工作目录无关。
#include <yomk/plugin_system/YomkPluginApi.h>
#include <yomk/server/YomkServerApi.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace yomk;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const std::string& desc)
{
    if (ok)
    {
        g_pass++;
        std::cout << "[PASS] " << desc << std::endl;
    }
    else
    {
        g_fail++;
        std::cout << "[FAIL] " << desc << std::endl;
    }
}

static bool isOk(const yomk::payload::Response& r)
{
    return r.status == yomk::payload::ResStatus::Succeeded;
}
static bool isNo(const yomk::payload::Response& r)
{
    return r.status == yomk::payload::ResStatus::Failed;
}

static std::string respString(const yomk::payload::Response& r)
{
    if (r.data && r.data->name() == "String")
    {
        auto p = std::dynamic_pointer_cast<yomk::msgs::String>(r.data);
        if (p)
        {
            return p->d;
        }
    }
    return "";
}

// 组合断言：Failed 且错误文案含子串
static void expectNo(const yomk::payload::Response& r, const std::string& substr, const std::string& desc)
{
    check(isNo(r) && r.msg.find(substr) != std::string::npos, desc);
}

// 组合断言：Succeeded 且汇总串精确匹配
static void expectOkSummary(const yomk::payload::Response& r, const std::string& summary, const std::string& desc)
{
    check(isOk(r) && respString(r) == summary, desc + " (" + summary + ")");
}

// 夹具：插件 so 路径 = env YOMK_TEST_PLUGIN 优先，回退编译期固化路径
static std::string pluginLibPath()
{
    const char* env = std::getenv("YOMK_TEST_PLUGIN");
    if (env && *env)
    {
        return env;
    }
    return TEST_PLUGIN_BUILD_PATH;
}

// 夹具根目录：<测试可执行目录>/manifest，跟随测试程序而非当前工作目录
static std::filesystem::path fixtureDir()
{
    namespace fs = std::filesystem;
    return fs::canonical("/proc/self/exe").parent_path() / "manifest";
}

// 夹具：夹具目录内文件完整路径
static std::string fp(const std::string& name)
{
    return (fixtureDir() / name).string();
}

// 夹具：写清单文件
static void writeManifest(const std::string& name, const std::string& content)
{
    std::ofstream f(fp(name));
    f << content;
}

// instances() 解包；失败返回空表
static std::vector<yomk::payload::InstanceInfo> listInstances()
{
    std::vector<yomk::payload::InstanceInfo> out;
    yomk::payload::Response resp = yomk::plugin().instances();
    if (resp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
        out = arr->d;
    }
    return out;
}

// plugins() 解包；失败返回空表
static std::vector<yomk::payload::PluginMeta> listPlugins()
{
    std::vector<yomk::payload::PluginMeta> out;
    yomk::payload::Response resp = yomk::plugin().plugins();
    if (resp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
        out = arr->d;
    }
    return out;
}

int main()
{
    yomk::server().init();
    check(yomk::server().srv<YomkPluginSystemBuilder>().create() == 0, "register YomkPluginSystemBuilder");

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all(fixtureDir(), ec);
    fs::create_directories(fixtureDir(), ec);
    check(fs::is_directory(fixtureDir()), "fixture dir prepared under test executable");

    fs::create_symlink(pluginLibPath(), fp("libTestPlugin.so"), ec);
    {
        std::ofstream f(fp("inst.txt"));
    }

    // ---------- S1 合法构建与内省基线 ----------
    writeManifest("s1.yomk", "#! yomk_plugin_system\ns1-i1@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("s1.yomk")).build(), "plugins:1 instances:1", "S1 build single entry ok");

    // 按插件名查询（两段链 plg_name 绑定 + get_info 终结，返回单条 PluginMeta）
    yomk::payload::Response infoResp = yomk::plugin().plg_name("TestPlugin").get_info();
    bool infoHit = false;
    if (isOk(infoResp))
    {
        auto meta = yomk::msgs::unpack<yomk::msgs::PluginMeta>(infoResp.data);
        infoHit = meta && meta->d.name == "TestPlugin" && meta->d.version == "0.0.1";
    }
    check(infoHit, "S1 plg_name().get_info() returns single TestPlugin meta");
    expectNo(
        yomk::plugin().plg_name("NoSuch").get_info(), "plugin not loaded: NoSuch", "S1 get_info unknown plugin rejects");

    std::vector<yomk::payload::InstanceInfo> insts = listInstances();
    check(insts.size() == 1, "S1 exactly one instance listed");
    check(
        !insts.empty() && insts[0].pluginName == "TestPlugin" && insts[0].instanceName == "TestPlugin.s1-i1",
        "S1 instance pluginName and name");
    check(!insts.empty() && insts[0].instanceType == "demo", "S1 instance type from plugin");

    std::vector<yomk::payload::PluginMeta> metas = listPlugins();
    check(metas.size() == 1, "S1 exactly one plugin listed");
    check(
        !metas.empty() && metas[0].name == "TestPlugin" && metas[0].type == "demo" && metas[0].version == "0.0.1",
        "S1 plugin meta name/type/version");
    check(
        !metas.empty() && metas[0].author == "Yomk" && metas[0].abi_version == YOMKPLUGIN_ABI_VERSION,
        "S1 plugin meta author and abi version");
    check(!metas.empty() && metas[0].path == fp("libTestPlugin.so"), "S1 plugin meta path");

    yomk::payload::Response resp =
        yomk::server().url("/YomkPluginSystemBuilder/plugin").msg(yomk::msgs::String::shared("TestPlugin")).request();
    bool queried = false;
    if (isOk(resp))
    {
        auto meta = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
        queried = meta && meta->d.name == "TestPlugin";
    }
    check(queried, "S1 proxy /plugin returns single plugin meta");

    // ---------- S2 so 复用幂等与实例重名 ----------
    writeManifest("s2.yomk", "#! yomk_plugin_system\ns2-i1@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("s2.yomk")).build(), "plugins:1 instances:1", "S2 same lib reused, not reloaded");
    writeManifest("s2dup.yomk", "#! yomk_plugin_system\ns1-i1@libTestPlugin.so@inst.txt\n");
    expectNo(
        yomk::plugin().manifest(fp("s2dup.yomk")).build(),
        "manifest line 2: create failed: duplicate instance name: s1-i1",
        "S2 duplicate instance name through build");
    check(listInstances().size() == 2, "S2 two instances alive after dup-build failure");

    // ---------- S3 create 重名与 destroy 重建（白盒 URL） ----------
    yomk::payload::CreateReq dupReq;
    dupReq.pluginName = "TestPlugin";
    dupReq.instanceName = "s2-i1";
    dupReq.instanceFile = fp("inst.txt");
    expectNo(
        yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(dupReq)).request(),
        "duplicate instance name: s2-i1",
        "S3 direct create duplicate rejects");

    yomk::payload::DestroyReq dreq;
    dreq.pluginName = "TestPlugin";
    dreq.instanceName = "s2-i1";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dreq)).request()), "S3 destroy s2-i1 ok");
    dreq.instanceName = "s1-i1";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dreq)).request()), "S3 destroy s1-i1 ok");
    check(listInstances().empty(), "S3 instances empty after destroys");
    expectNo(
        yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dreq)).request(),
        "instance not found: TestPlugin/s1-i1",
        "S3 destroy nonexistent rejects");
    yomk::payload::CreateReq reReq;
    reReq.pluginName = "TestPlugin";
    reReq.instanceName = "s1-i1";
    reReq.instanceFile = fp("inst.txt");
    check(
        isOk(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(reReq)).request()),
        "S3 recreate after destroy ok");
    check(listInstances().size() == 1, "S3 instance back after recreate");

    // ---------- S4 try_unload 拒绝与放行 ----------
    expectNo(
        yomk::plugin().plg_name("TestPlugin").try_unload(),
        "instances still alive: TestPlugin",
        "S4 try_unload rejected while instance alive");
    dreq.instanceName = "s1-i1";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dreq)).request()),
        "S4 destroy for unload");
    check(isOk(yomk::plugin().plg_name("TestPlugin").try_unload()), "S4 try_unload ok after destroy");
    check(listPlugins().empty(), "S4 plugins empty after try_unload");
    expectNo(yomk::plugin().plg_name("NoSuchLib").try_unload(), "plugin not loaded: NoSuchLib", "S4 try_unload unknown plugin");

    // ---------- S5 unload 强卸含存活实例 ----------
    writeManifest("s5.yomk", "#! yomk_plugin_system\ns5-i1@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("s5.yomk")).build(), "plugins:1 instances:1", "S5 rebuild for force unload");
    check(isOk(yomk::plugin().plg_name("TestPlugin").unload()), "S5 force unload with alive instance ok");
    check(listInstances().empty(), "S5 instances destroyed by force unload");
    expectNo(yomk::plugin().plg_name("NoSuchLib").unload(), "plugin not loaded: NoSuchLib", "S5 unload unknown plugin");

    // ---------- S6 卸载后重载 ----------
    writeManifest("s6.yomk", "#! yomk_plugin_system\ns6-i1@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("s6.yomk")).build(), "plugins:1 instances:1", "S6 reload after unload ok");
    insts = listInstances();
    check(insts.size() == 1 && insts[0].instanceName == "TestPlugin.s6-i1", "S6 instance back after reload");

    // ---------- S7 缺文件行号错误 ----------
    writeManifest("s7a.yomk", "#! yomk_plugin_system\ns7-i1@no_such.so@inst.txt\n");
    expectNo(yomk::plugin().manifest(fp("s7a.yomk")).build(), "manifest line 2: plugin lib not found", "S7 missing lib path error");
    writeManifest("s7b.yomk", "#! yomk_plugin_system\ns7-i2@libTestPlugin.so@missing_inst.txt\n");
    expectNo(
        yomk::plugin().manifest(fp("s7b.yomk")).build(), "manifest line 2: instance file not found", "S7 missing instance file error");
    writeManifest("s7c.yomk", "#! yomk_plugin_system\ns7-i3@no_such.so@missing_inst.txt\n");
    expectNo(yomk::plugin().manifest(fp("s7c.yomk")).build(), "instance file not found", "S7 instance file checked before lib");

    // ---------- 清理现场 ----------
    check(isOk(yomk::plugin().plg_name("TestPlugin").unload()), "cleanup: force_unload TestPlugin");
    check(listInstances().empty(), "cleanup: instances empty after unload");

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
