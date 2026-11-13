// TestYomkPluginManagerEdge：Manager 边界与内省细节测试（闭环5）
// 白盒 URL 直调 YomkPluginManager 与 YomkPluginLoader 系列接口，覆盖：
// create/destroy 入参边界、list/list_instances filter 三态（未知/命中/已注册零实例）、
// instanceInfoLine 内省行、Loader meta 深拷贝、插件注册列表、跨插件同名实例、超长输入边界。
// 夹具模式同前：<测试可执行目录>/manifest，与工作目录无关。
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

// 夹具：符号链接插件 so 到夹具目录（清单 lib 段用相对路径引用）
static void linkLib(const char* compileTimePath, const std::string& linkName)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove(fp(linkName), ec);
    fs::create_symlink(compileTimePath, fp(linkName), ec);
    if (ec)
    {
        std::cout << "[FAIL] symlink " << linkName << " failed: " << ec.message() << std::endl;
        g_fail++;
    }
}

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

int main()
{
    yomk::server().init();
    check(yomk::server().srv<YomkPluginSystemBuilder>().create() == 0, "register YomkPluginSystemBuilder");

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all(fixtureDir(), ec);
    fs::create_directories(fixtureDir(), ec);
    check(fs::is_directory(fixtureDir()), "fixture dir prepared under test executable");

    linkLib(TEST_PLUGIN_BUILD_PATH, "libTestPlugin.so");
    linkLib(SECOND_PLUGIN_BUILD_PATH, "libSecondPlugin.so");
    linkLib(BAD_CREATE_NULL_PATH, "libBadCreateNull.so");
    {
        std::ofstream f(fp("inst.txt"));
    }

    // 基线：TestPlugin 双实例 + BadCreateNull 注册（0 实例）
    writeManifest("base.yomk", "#! yomk_plugin_system\ne-a@libTestPlugin.so@inst.txt\ne-b@libTestPlugin.so@inst.txt\n");
    check(isOk(yomk::plugin().manifest(fp("base.yomk")).build()), "build baseline with two instances");
    yomk::payload::PluginPath badPath;
    badPath.path = fp("libBadCreateNull.so");
    check(
        isOk(yomk::server().url("/YomkPluginManager/load").msg(yomk::msgs::PluginPath::shared(badPath)).request()),
        "whitebox load BadCreateNull (registers with 0 instances)");

    // ---------- M1 create/destroy 入参边界 ----------
    yomk::payload::CreateReq emptyName;
    emptyName.pluginName = "TestPlugin";
    emptyName.instanceName = "";
    emptyName.instanceCfg = fp("inst.txt");
    expectNo(
        yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(emptyName)).request(),
        "instance name required",
        "M1 create with empty name rejects");
    yomk::payload::CreateReq emptyLib;
    emptyLib.pluginName = "";
    emptyLib.instanceName = "e-x";
    expectNo(
        yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(emptyLib)).request(),
        "plugin not loaded: ",
        "M1 create with empty pluginName rejects");
    yomk::payload::CreateReq unknownLib;
    unknownLib.pluginName = "NoSuch";
    unknownLib.instanceName = "e-x";
    expectNo(
        yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(unknownLib)).request(),
        "plugin not loaded: NoSuch",
        "M1 create with unknown pluginName rejects");
    yomk::payload::DestroyReq emptyDestroy;
    emptyDestroy.pluginName = "";
    emptyDestroy.instanceName = "";
    expectNo(
        yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(emptyDestroy)).request(),
        "instance not found: /",
        "M1 destroy with empty req rejects");

    // ---------- M2 列表与单查分离 ----------
    yomk::payload::Response resp = yomk::server().url("/YomkPluginManager/list").request();
    bool listOk = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
        listOk = arr && arr->d.size() >= 2;
    }
    check(listOk, "M2 list returns all loaded plugins");

    resp = yomk::server().url("/YomkPluginManager/plugin").msg(yomk::msgs::String::shared("TestPlugin")).request();
    bool hitOne = false;
    if (isOk(resp))
    {
        auto meta = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
        hitOne = meta && meta->d.name == "TestPlugin";
    }
    check(hitOne, "M2 plugin query returns single plugin meta");
    expectNo(
        yomk::server().url("/YomkPluginManager/plugin").msg(yomk::msgs::String::shared("Nope")).request(),
        "plugin not loaded: Nope",
        "M2 plugin query miss rejects");

    resp = yomk::server().url("/YomkPluginManager/list_instances").msg(yomk::msgs::String::shared("TestPlugin")).request();
    bool hitTwo = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
        hitTwo = arr->d.size() == 2;
    }
    check(hitTwo, "M2 list_instances filter hit returns 2 instances");
    expectNo(
        yomk::server().url("/YomkPluginManager/list_instances").msg(yomk::msgs::String::shared("NoSuch")).request(),
        "plugin not loaded: NoSuch",
        "M2 list_instances unknown plugin rejects");
    resp = yomk::server().url("/YomkPluginManager/list_instances").msg(yomk::msgs::String::shared("BadCreateNull")).request();
    bool zeroOk = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
        zeroOk = arr->d.empty();
    }
    check(zeroOk, "M2 list_instances registered-but-zero returns ok empty");

    resp = yomk::server().url("/YomkPluginSystemBuilder/plugin").msg(yomk::msgs::String::shared("TestPlugin")).request();
    check(isOk(resp), "M2 builder proxy /plugin hit");

    // ---------- M2.5 链式门面：plg_name(name).instances() 与直调 filter 等价 ----------
    resp = yomk::plugin().plg_name("TestPlugin").instances();
    bool macroHitTwo = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
        macroHitTwo = arr->d.size() == 2;
    }
    check(macroHitTwo, "M2 plg_name(name).instances() filter hit returns 2 instances");
    expectNo(
        yomk::plugin().plg_name("NoSuch").instances(),
        "plugin not loaded: NoSuch",
        "M2 plg_name(name).instances() unknown plugin rejects");

    // ---------- M2.6 链式门面：inst_name(name).get_plg_info() 反查所属插件 ----------
    resp = yomk::plugin().inst_name("TestPlugin.e-a").get_plg_info();
    bool byInstOk = false;
    if (isOk(resp))
    {
        auto pm = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
        byInstOk = pm && pm->d.name == "TestPlugin" && pm->d.type == "demo" && pm->d.path == fp("libTestPlugin.so");
    }
    check(byInstOk, "M2 inst_name(name).get_plg_info() instance lookup returns owner plugin meta");
    expectNo(
        yomk::plugin().inst_name("no-inst").get_plg_info(),
        "instance not found: no-inst",
        "M2 inst_name(name).get_plg_info() unknown instance rejects");

    // ---------- M2.7 链式门面：inst_cfg(instanceName) 返回实例配置内容 ----------
    {
        std::ofstream f(fp("mfile.txt"));
        f << "line-one\nline-two";
    }
    yomk::payload::CreateReq mfileReq;
    mfileReq.pluginName = "TestPlugin";
    mfileReq.instanceName = "e-mfile";
    mfileReq.instanceCfg = fp("mfile.txt");
    check(
        isOk(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(mfileReq)).request()),
        "M2 create e-mfile for config introspection");
    resp = yomk::plugin().inst_cfg("TestPlugin.e-mfile");
    check(isOk(resp) && respString(resp) == "line-one\nline-two", "M2 inst_cfg(instanceName) returns config content");
    yomk::payload::CreateReq nofileReq;
    nofileReq.pluginName = "TestPlugin";
    nofileReq.instanceName = "e-nofile";
    check(
        isOk(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(nofileReq)).request()),
        "M2 create e-nofile with empty instanceCfg");
    expectNo(
        yomk::plugin().inst_cfg("TestPlugin.e-nofile"),
        "instance config not recorded: TestPlugin.e-nofile",
        "M2 inst_cfg(instanceName) empty instanceCfg rejects");
    expectNo(
        yomk::plugin().inst_cfg("no-inst"),
        "instance not found: no-inst",
        "M2 inst_cfg(instanceName) unknown instance rejects");
    yomk::payload::DestroyReq dr;
    dr.pluginName = "TestPlugin";
    dr.instanceName = "e-mfile";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dr)).request()),
        "M2 cleanup e-mfile");
    dr.instanceName = "e-nofile";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dr)).request()),
        "M2 cleanup e-nofile");

    // ---------- M3 内省格式与列表 ----------
    resp = yomk::server().url("/YomkPluginLoader/meta").msg(yomk::msgs::String::shared("TestPlugin")).request();
    bool metaOk = false;
    if (isOk(resp))
    {
        auto m = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
        metaOk = m->d.abi_version == YOMKPLUGIN_ABI_VERSION && m->d.name == "TestPlugin" && m->d.type == "demo" &&
                 m->d.version == "0.0.1" && m->d.author == "Yomk";
    }
    check(metaOk, "M3 loader /meta deep-copied fields");

    bool hasTest = false;
    bool hasBad = false;
    for (const auto& m : listPlugins())
    {
        if (m.name == "TestPlugin")
        {
            hasTest = true;
        }
        if (m.name == "BadCreateNull")
        {
            hasBad = true;
        }
    }
    check(hasTest && hasBad, "M3 plugins list contains both registered plugins");

    // ---------- M5 超长输入边界 ----------
    const std::string longName(4096, 'x');
    yomk::payload::CreateReq longNameReq;
    longNameReq.pluginName = "TestPlugin";
    longNameReq.instanceName = longName;
    longNameReq.instanceCfg = fp("inst.txt");
    check(
        isOk(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(longNameReq)).request()),
        "M5 create with 4096-char name ok");
    bool longVisible = false;
    for (const auto& i : listInstances())
    {
        if (i.instanceName == "TestPlugin." + longName)
        {
            longVisible = true;
        }
    }
    check(longVisible, "M5 4096-char name visible in list");
    yomk::payload::DestroyReq dn;
    dn.pluginName = "TestPlugin";
    dn.instanceName = longName;
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dn)).request()),
        "M5 destroy 4096-char name ok");

    const std::string longFile = fp(std::string(8000, 'f') + ".txt");
    yomk::payload::CreateReq longFileReq;
    longFileReq.pluginName = "TestPlugin";
    longFileReq.instanceName = "e-longfile";
    longFileReq.instanceCfg = longFile;
    check(
        isOk(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(longFileReq)).request()),
        "M5 create with ~8KB instance cfg path ok");
    yomk::payload::DestroyReq df;
    df.pluginName = "TestPlugin";
    df.instanceName = "e-longfile";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(df)).request()),
        "M5 destroy long-file-path instance ok");

    // ---------- M5.5 错误类型消息请求（闭环10 回归）：解包校验失败返回 Failed 而非崩溃 ----------
    check(isNo(yomk::server().url("/YomkPluginLoader/load").msg(yomk::msgs::String::shared("wrong-type-probe")).request()),
          "wrong-type msg to /YomkPluginLoader/load returns Failed (no crash)");
    check(isNo(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::String::shared("wrong-type-probe")).request()),
          "wrong-type msg to /YomkPluginManager/create_instance returns Failed (no crash)");
    check(isNo(yomk::server().url("/YomkPluginSystemBuilder/build").msg(yomk::msgs::String::shared("wrong-type-probe")).request()),
          "wrong-type msg to /YomkPluginSystemBuilder/build returns Failed (no crash)");

    // ---------- M6 跨插件同名实例（限定名 = 插件名.实例名，全局唯一） ----------
    yomk::payload::PluginPath secondPath;
    secondPath.path = fp("libSecondPlugin.so");
    check(isOk(yomk::server().url("/YomkPluginManager/load").msg(yomk::msgs::PluginPath::shared(secondPath)).request()), "M6 load SecondPlugin");

    yomk::payload::CreateReq crossA;
    crossA.pluginName = "TestPlugin";
    crossA.instanceName = "shared-i";
    crossA.instanceCfg = fp("inst.txt");
    check(
        isOk(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(crossA)).request()),
        "M6 create shared-i in TestPlugin");
    yomk::payload::CreateReq crossB;
    crossB.pluginName = "SecondPlugin";
    crossB.instanceName = "shared-i";
    crossB.instanceCfg = fp("inst.txt");
    check(
        isOk(yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(crossB)).request()),
        "M6 create same bare name in SecondPlugin (cross-plugin dup allowed)");

    resp = yomk::plugin().inst_name("TestPlugin.shared-i").get_plg_info();
    bool ownerA = false;
    if (isOk(resp))
    {
        auto pmA = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
        ownerA = pmA && pmA->d.name == "TestPlugin";
    }
    check(ownerA, "M6 qualified name resolves to TestPlugin");
    resp = yomk::plugin().inst_name("SecondPlugin.shared-i").get_plg_info();
    bool ownerB = false;
    if (isOk(resp))
    {
        auto pmB = yomk::msgs::unpack<yomk::msgs::PluginMeta>(resp.data);
        ownerB = pmB && pmB->d.name == "SecondPlugin";
    }
    check(ownerB, "M6 same bare name under second plugin resolves to SecondPlugin");

    size_t qualifiedHits = 0;
    for (const auto& i : listInstances())
    {
        if (i.instanceName == "TestPlugin.shared-i" || i.instanceName == "SecondPlugin.shared-i")
        {
            ++qualifiedHits;
        }
    }
    check(qualifiedHits == 2, "M6 list shows both qualified names");

    yomk::payload::DestroyReq dcrossA;
    dcrossA.pluginName = "TestPlugin";
    dcrossA.instanceName = "shared-i";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dcrossA)).request()),
        "M6 destroy TestPlugin/shared-i");
    yomk::payload::DestroyReq dcrossB;
    dcrossB.pluginName = "SecondPlugin";
    dcrossB.instanceName = "shared-i";
    check(
        isOk(yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(dcrossB)).request()),
        "M6 destroy SecondPlugin/shared-i");
    check(isOk(yomk::plugin().plg_name("SecondPlugin").unload()), "M6 cleanup: force_unload SecondPlugin");

    // ---------- M7 清理现场 ----------
    check(isOk(yomk::plugin().plg_name("BadCreateNull").unload()), "cleanup: force_unload BadCreateNull");
    check(isOk(yomk::plugin().plg_name("TestPlugin").unload()), "cleanup: force_unload TestPlugin");
    check(listPlugins().empty(), "cleanup: plugins empty after unloads");

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
