// TestYomkPluginReload：重复加载/卸载时序与状态机测试（闭环6）
// 覆盖：双层去重（同路径重复 load、同 meta 不同路径 so 副本）、
// alive 计数随 destroy 的 purgeDead 时序、try_unload/force_unload 空卸组合、
// 卸载-重载全循环、多实例强卸。
// 夹具模式同 TestYomkPluginBuildLifecycle：<测试可执行目录>/manifest，与工作目录无关。
#include <yomk/plug_sys/YomkPlugSysApi.h>
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
    check(yomk::server().srv<YomkPlugSysBuilder>().create() == 0, "register YomkPlugSysBuilder");

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all(fixtureDir(), ec);
    fs::create_directories(fixtureDir(), ec);
    check(fs::is_directory(fixtureDir()), "fixture dir prepared under test executable");

    // 主条目 so 用符号链接；去重副本用真实拷贝（符号链接会被解析为同一路径，无法构造不同路径同 meta）
    fs::create_symlink(pluginLibPath(), fp("libTestPlugin.so"), ec);
    fs::copy_file(pluginLibPath(), fp("libTestPluginCopy.so"), fs::copy_options::overwrite_existing, ec);
    check(!ec && fs::exists(fp("libTestPluginCopy.so")), "copy plugin so fixture ok");
    {
        std::ofstream f(fp("inst.txt"));
    }

    // ---------- R1 双层去重：基线双实例 ----------
    writeManifest("r1.yomk", "#! yomk_plug_sys\nr-a@libTestPlugin.so@inst.txt\nr-b@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("r1.yomk")).build(), "plugins:1 instances:2", "R1 build two instances baseline");

    // 同路径重复 load：Manager /load 透传 Loader 按 pluginName 判重
    YomkMsgPtr samePath = yomk::msgs::PluginPath::shared(yomk::payload::PluginPath{fp("libTestPlugin.so")});
    expectNo(
        yomk::server().url("/YomkPluginManager/load").msg(samePath).request(),
        "plugin already loaded: TestPlugin",
        "R1 load same path rejects");

    // 不同路径同 meta 的 so 副本：判重与路径无关，同样命中
    YomkMsgPtr copyPath = yomk::msgs::PluginPath::shared(yomk::payload::PluginPath{fp("libTestPluginCopy.so")});
    expectNo(
        yomk::server().url("/YomkPluginManager/load").msg(copyPath).request(),
        "plugin already loaded: TestPlugin",
        "R1 load copy so with same meta rejects");

    // 失败路径无状态污染：插件表仍恰 1 条，实例仍 2 个
    std::vector<yomk::payload::PluginMeta> names = listPlugins();
    check(
        names.size() == 1 && !names.empty() && names[0].name == "TestPlugin",
        "R1 manager list still exactly one TestPlugin after rejected loads");
    check(listInstances().size() == 2, "R1 instances still two after rejected loads");

    // ---------- R2 存活表递减：destroy 后引用保护随之解除 ----------
    yomk::payload::DestroyReq dreq;
    dreq.pluginName = "TestPlugin";
    dreq.instanceName = "r-b";
    check(isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dreq)).request()), "R2 destroy r-b ok");
    expectNo(yomk::plugin().plg_name("TestPlugin").try_unload(), "instances still alive", "R2 try_unload rejects after destroy r-b");
    dreq.instanceName = "r-a";
    check(isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dreq)).request()), "R2 destroy r-a ok");
    check(listInstances().empty(), "R2 instances empty after all destroys");

    // ---------- R3 空卸与重载循环 ----------
    check(isOk(yomk::plugin().plg_name("TestPlugin").try_unload()), "R3 try_unload ok while alive:0");
    check(listPlugins().empty(), "R3 plugins empty after try_unload");
    expectNo(yomk::plugin().plg_name("TestPlugin").try_unload(), "plugin not loaded: TestPlugin", "R3 try_unload again rejects");
    expectNo(
        yomk::plugin().plg_name("TestPlugin").unload(),
        "plugin not loaded: TestPlugin",
        "R3 force unload not-loaded rejects with same message");

    // 重新构建触发二次 dlopen：重载后去重仍生效
    writeManifest("r3.yomk", "#! yomk_plug_sys\nr3-i1@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("r3.yomk")).build(), "plugins:1 instances:1", "R3 rebuild after unload ok");
    expectNo(
        yomk::server().url("/YomkPluginManager/load").msg(samePath).request(),
        "plugin already loaded: TestPlugin",
        "R3 load rejects after reload");

    // R4 前置清理：destroy 重载实例（插件保持已加载）
    dreq.instanceName = "r3-i1";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dreq)).request()),
        "R3 destroy r3-i1 for R4 prep");

    // ---------- R4 多实例强卸 ----------
    writeManifest(
        "r4.yomk",
        "#! "
        "yomk_plug_sys\nr4-a@libTestPlugin.so@inst.txt\nr4-b@libTestPlugin.so@inst.txt\nr4-c@libTestPlugin.so@"
        "inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("r4.yomk")).build(), "plugins:1 instances:3", "R4 build three instances");
    expectNo(yomk::plugin().plg_name("TestPlugin").try_unload(), "instances still alive", "R4 try_unload rejects while three alive");
    check(isOk(yomk::plugin().plg_name("TestPlugin").unload()), "R4 force unload with three alive instances ok");
    check(listInstances().empty(), "R4 instances destroyed by force unload");
    check(listPlugins().empty(), "R4 plugins empty after force unload");

    // ---------- R5 终态清零 ----------
    check(listPlugins().empty(), "R5 plugins empty at end");
    check(listInstances().empty(), "R5 instances empty at end");

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
