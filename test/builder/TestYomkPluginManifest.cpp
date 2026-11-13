// TestYomkPluginManifest：清单解析全面测试（闭环2）
// 经 YOMKPLUGIN_BUILD 触达 parseManifest 全分支：路径预检、首行格式标识、
// 注释与空行、实例块与键值严格校验、绝对路径拒绝、块头行号计数、合法块接受，
// 并对账 /builder（recordBuild 侧效：manifest 条目/path/result/计数）。
// 清单夹具生成于 <测试可执行目录>/manifest（跟随测试程序，与工作目录无关，
// 直接运行也不污染当前目录）；插件 so 符号链接进夹具目录满足相对路径契约。
#include <yomk/plug_sys/YomkPlugSysApi.h>
#include <yomk/server/YomkServerApi.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

// 组合断言：Failed 且错误文案含子串
static void expectNo(const yomk::payload::Response& r, const std::string& substr, const std::string& desc)
{
    check(isNo(r) && r.msg.find(substr) != std::string::npos, desc);
}

// 组合断言：Succeeded 且 BuilderInfo 快照的汇总与期望一致
static void expectOkSummary(const yomk::payload::Response& r, const std::string& summary, const std::string& desc)
{
    auto info = isOk(r) ? yomk::msgs::unpack<yomk::msgs::BuilderInfo>(r.data) : nullptr;
    const std::string actual =
        info ? "plugins:" + std::to_string(info->d.plugins) + " instances:" + std::to_string(info->d.instances) : "";
    check(isOk(r) && actual == summary, desc + " (" + summary + ")");
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

// 夹具：夹具目录内文件完整路径（清单/实例配置/符号链接统一走此路径）
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

// 夹具：空实例配置（内容透传不读）
static void touchInstanceCfg(const std::string& name)
{
    std::ofstream f(fp(name));
}

// 夹具：符号链接插件 so 到夹具目录（清单 lib 键用相对路径引用）
static void linkPlugin()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove(fp("libTestPlugin.so"), ec);
    fs::create_symlink(pluginLibPath(), fp("libTestPlugin.so"), ec);
    if (ec)
    {
        std::cout << "[FAIL] symlink plugin lib failed: " << ec.message() << std::endl;
        g_fail++;
    }
}

// 最近构建信息（对账 recordBuild 侧效）
static yomk::payload::BuilderInfo builderInfo()
{
    yomk::payload::Response resp = yomk::plug_sys().builder();
    if (resp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto info = yomk::msgs::unpack<yomk::msgs::BuilderInfo>(resp.data);
        if (info)
        {
            return info->d;
        }
    }
    return yomk::payload::BuilderInfo{};
}

int main()
{
    yomk::server().init();
    check(yomk::server().srv<YomkPlugSysBuilder>().create() == 0, "register YomkPlugSysBuilder");

    namespace fs = std::filesystem;
    // 夹具目录每次运行清空重建，保证干净起点
    std::error_code ec;
    fs::remove_all(fixtureDir(), ec);
    fs::create_directories(fixtureDir(), ec);
    check(fs::is_directory(fixtureDir()), "fixture dir prepared under test executable");

    linkPlugin();
    touchInstanceCfg("inst.txt");

    // ---------- A 路径预检：不存在/空串/目录/空白串 ----------
    expectNo(yomk::plug_sys().manifest("/nonexistent/no_such.yomk").build(), "manifest not found", "A1 nonexistent path rejects");
    expectNo(yomk::plug_sys().manifest("").build(), "manifest not found", "A2 empty path rejects");
    fs::create_directory(fp("adirectory"), ec);
    expectNo(yomk::plug_sys().manifest(fp("adirectory")).build(), "manifest not found", "A3 directory path rejects");
    expectNo(yomk::plug_sys().manifest("   ").build(), "manifest not found", "A4 whitespace-only path rejects");

    // ---------- B 首行格式标识 ----------
    writeManifest("m-empty.yomk", "");
    expectNo(yomk::plug_sys().manifest(fp("m-empty.yomk")).build(), "format marker not found", "B1 empty file rejects marker");
    writeManifest("m-wrong.yomk", "#! other_format\n");
    expectNo(yomk::plug_sys().manifest(fp("m-wrong.yomk")).build(), "format marker not found", "B2 wrong marker rejects");
    writeManifest("m-pad.yomk", "  #! yomk_plug_sys\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-pad.yomk")).build(), "plugins:0 instances:0", "B3 leading-space marker accepts");
    writeManifest("m-trail.yomk", "#! yomk_plug_sys trailing\n");
    expectNo(yomk::plug_sys().manifest(fp("m-trail.yomk")).build(), "format marker not found", "B4 marker trailing content rejects");
    writeManifest("m-notfirst.yomk", "[x]\n#! yomk_plug_sys\n");
    expectNo(yomk::plug_sys().manifest(fp("m-notfirst.yomk")).build(), "line 1: format marker not found", "B5 marker not first line");
    writeManifest("m-crlf.yomk", "#! yomk_plug_sys\r\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-crlf.yomk")).build(), "plugins:0 instances:0", "B6 marker line CRLF accepts");
    writeManifest("m-markcmt.yomk", "#! yomk_plug_sys    # 文件标识\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-markcmt.yomk")).build(), "plugins:0 instances:0", "B7 inline comment after marker accepts");

    // ---------- C 注释与空行 ----------
    writeManifest("m-comments.yomk", "#! yomk_plug_sys\n# c1\n# c2\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-comments.yomk")).build(), "plugins:0 instances:0", "C1 all-comment manifest ok");
    writeManifest("m-blanks.yomk", "#! yomk_plug_sys\n\n   \n\t\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-blanks.yomk")).build(), "plugins:0 instances:0", "C2 blank/whitespace lines skip");
    writeManifest(
        "m-inlinekv.yomk",
        "#! yomk_plug_sys\n[i-inline]\nlib = libTestPlugin.so # lib comment\nconfig = inst.txt # cfg comment\n");
    expectOkSummary(
        yomk::plug_sys().manifest(fp("m-inlinekv.yomk")).build(), "plugins:1 instances:1", "C3 inline comment on key lines");
    writeManifest(
        "m-inlinehead.yomk", "#! yomk_plug_sys\n[i-head] # instance block\nlib = libTestPlugin.so\nconfig = inst.txt\n");
    expectOkSummary(
        yomk::plug_sys().manifest(fp("m-inlinehead.yomk")).build(), "plugins:1 instances:1", "C4 inline comment on block header");

    // ---------- D 实例块与键值结构 ----------
    writeManifest("m-d1.yomk", "#! yomk_plug_sys\nlib = libTestPlugin.so\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d1.yomk")).build(), "key outside instance block", "D1 key before any block rejects");
    writeManifest("m-d2.yomk", "#! yomk_plug_sys\n[bad\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d2.yomk")).build(), "malformed instance block header",
        "D2 header without ']' rejects");
    writeManifest("m-d3.yomk", "#! yomk_plug_sys\n[]\n");
    expectNo(yomk::plug_sys().manifest(fp("m-d3.yomk")).build(), "empty instance name", "D3 empty block name rejects");
    writeManifest("m-d4.yomk", "#! yomk_plug_sys\n[a.b]\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d4.yomk")).build(), "instance name must not contain '.'",
        "D4 dotted block name rejects");
    writeManifest("m-d5.yomk", "#! yomk_plug_sys\n[a]]\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d5.yomk")).build(), "instance name must not contain brackets",
        "D5 bracketed block name rejects");
    writeManifest("m-d6.yomk", "#! yomk_plug_sys\n[i]\nlib libTestPlugin.so\n");
    expectNo(yomk::plug_sys().manifest(fp("m-d6.yomk")).build(), "expect key = value", "D6 key line without '=' rejects");
    writeManifest("m-d7.yomk", "#! yomk_plug_sys\n[i]\nfoo = bar\n");
    expectNo(yomk::plug_sys().manifest(fp("m-d7.yomk")).build(), "unknown key 'foo'", "D7 unknown key rejects");
    writeManifest(
        "m-d8.yomk", "#! yomk_plug_sys\n[i]\nlib = libTestPlugin.so\nlib = libTestPlugin.so\n");
    expectNo(yomk::plug_sys().manifest(fp("m-d8.yomk")).build(), "duplicate key 'lib'", "D8 duplicate lib key rejects");
    writeManifest("m-d9.yomk", "#! yomk_plug_sys\n[i]\nconfig = inst.txt\nconfig = inst.txt\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d9.yomk")).build(), "duplicate key 'config'", "D9 duplicate config key rejects");
    writeManifest("m-d10.yomk", "#! yomk_plug_sys\n[i]\nlib =\n");
    expectNo(yomk::plug_sys().manifest(fp("m-d10.yomk")).build(), "empty value for 'lib'", "D10 empty lib value rejects");
    writeManifest("m-d11.yomk", "#! yomk_plug_sys\n[i]\nlib = libTestPlugin.so\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d11.yomk")).build(), "instance block [i] missing config",
        "D11 block missing config at EOF rejects");
    writeManifest("m-d12.yomk", "#! yomk_plug_sys\n[i-a]\nconfig = inst.txt\n[i-b]\nlib = libTestPlugin.so\nconfig = inst.txt\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d12.yomk")).build(), "instance block [i-a] missing lib",
        "D12 block missing lib at next header rejects");
    writeManifest("m-d13.yomk", "#! yomk_plug_sys\n[empty]\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-d13.yomk")).build(), "instance block [empty] missing lib and config",
        "D13 empty block rejects");

    // ---------- E 绝对路径拒绝 ----------
    writeManifest("m-e1.yomk", "#! yomk_plug_sys\n[a]\nlib = /abs/lib.so\nconfig = inst.txt\n");
    expectNo(yomk::plug_sys().manifest(fp("m-e1.yomk")).build(), "lib path must be relative", "E1 absolute lib path rejects");
    writeManifest("m-e2.yomk", "#! yomk_plug_sys\n[a]\nlib = libTestPlugin.so\nconfig = /abs/inst.txt\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-e2.yomk")).build(), "instance config path must be relative",
        "E2 absolute config path rejects");

    // ---------- F 行号计数（注释/空行跳过后行号正确） ----------
    writeManifest("m-f1.yomk", "#! yomk_plug_sys\nlib = libTestPlugin.so\n");
    expectNo(yomk::plug_sys().manifest(fp("m-f1.yomk")).build(), "manifest line 2:", "F1 error reported on line 2");
    writeManifest("m-f2.yomk", "#! yomk_plug_sys\n# comment\n[i]\nfoo = bar\n");
    expectNo(yomk::plug_sys().manifest(fp("m-f2.yomk")).build(), "manifest line 4:", "F2 error reported on line 4");
    writeManifest("m-f3.yomk", "#! yomk_plug_sys\n# c\n[i]\nlib = libTestPlugin.so\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-f3.yomk")).build(), "manifest line 3: instance block [i] missing config",
        "F3 missing key reported on block header line 3");

    // ---------- G 合法实例块接受（真实加载 TestPlugin） ----------
    writeManifest("m-g1.yomk", "#! yomk_plug_sys\n[i-single]\nlib = libTestPlugin.so\nconfig = inst.txt\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-g1.yomk")).build(), "plugins:1 instances:1", "G1 single block builds");
    writeManifest(
        "m-g2.yomk",
        "#! yomk_plug_sys\n[i-a]\nlib = libTestPlugin.so\nconfig = inst.txt\n[i-b]\nlib = libTestPlugin.so\nconfig = inst.txt\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-g2.yomk")).build(), "plugins:1 instances:2", "G2 two blocks same lib");
    writeManifest(
        "m-g3.yomk", "#! yomk_plug_sys\n\t[ i-pad ]\t\nconfig =\t inst.txt \t\nlib =  libTestPlugin.so \t\n");
    expectOkSummary(
        yomk::plug_sys().manifest(fp("m-g3.yomk")).build(), "plugins:1 instances:1", "G3 padded values and reversed key order");
    writeManifest(
        "m-g4.yomk", "#! yomk_plug_sys\r\n[i-crlf]\r\nlib = libTestPlugin.so\r\nconfig = inst.txt\r\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-g4.yomk")).build(), "plugins:1 instances:1", "G4 block lines CRLF accepts");
    writeManifest("m-g5.yomk", "#! yomk_plug_sys\n[my inst]\nlib = libTestPlugin.so\nconfig = inst.txt\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("m-g5.yomk")).build(), "plugins:1 instances:1", "G5 space in block name accepts");

    // ---------- H /builder 对账（recordBuild 侧效） ----------
    const yomk::payload::BuilderInfo dump = builderInfo();
    check(
        dump.path == fp("m-g5.yomk") && dump.result == "ok" && dump.plugins == 1 && dump.instances == 1,
        "H1 /builder shows manifest path and ok result");
    check(
        std::any_of(
            dump.manifest.begin(),
            dump.manifest.end(),
            [](const yomk::payload::InstanceInfo& e) {
                return e.pluginName == "TestPlugin" && e.instanceName == "TestPlugin.my inst" &&
                       e.libFile == fp("libTestPlugin.so") && e.configFile == fp("inst.txt");
            }),
        "H2 /builder keeps built instance entries");

    writeManifest("m-hfail.yomk", "#! yomk_plug_sys\nlib = libTestPlugin.so\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-hfail.yomk")).build(), "key outside instance block", "H3 failure build Failed");
    const yomk::payload::BuilderInfo hfail = builderInfo();
    check(
        hfail.path == fp("m-hfail.yomk") && hfail.result == "manifest line 2: key outside instance block" &&
            hfail.plugins == 0 && hfail.instances == 0 && hfail.manifest.empty(),
        "H4 /builder records failure result");

    writeManifest(
        "m-part.yomk",
        "#! yomk_plug_sys\n[good1]\nlib = libTestPlugin.so\nconfig = inst.txt\n"
        "[bad2]\nlib = libTestPlugin.so\nconfig = missing_inst.txt\n");
    expectNo(
        yomk::plug_sys().manifest(fp("m-part.yomk")).build(), "manifest line 5: instance config not found",
        "H5 partial build fails on second block");
    const yomk::payload::BuilderInfo partDump = builderInfo();
    check(
        partDump.path == fp("m-part.yomk") &&
            partDump.result == "manifest line 5: instance config not found: " + fp("missing_inst.txt") &&
            partDump.plugins == 1 && partDump.instances == 1,
        "H6 /builder records partial failure with partial counts");
    check(
        std::any_of(
            partDump.manifest.begin(),
            partDump.manifest.end(),
            [](const yomk::payload::InstanceInfo& e) {
                return e.instanceName == "TestPlugin.good1" && e.libFile == fp("libTestPlugin.so") &&
                       e.configFile == fp("inst.txt");
            }),
        "H7 /builder keeps completed instance entries");

    // ---------- 清理现场 ----------
    check(isOk(yomk::plug_sys().plg_name("TestPlugin").unload()), "cleanup: force_unload TestPlugin");
    yomk::payload::Response inst = yomk::plug_sys().instances();
    bool instEmpty = false;
    if (isOk(inst))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(inst.data);
        instEmpty = arr->d.empty();
    }
    check(instEmpty, "cleanup: instances empty after unload");

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
