// TestYomkPluginManifest：清单解析全面测试（闭环2）
// 经 YOMKPLUGIN_BUILD 触达 parseManifest 全分支：路径预检、首行格式标识、
// 注释与空行、@ 切分与空段、绝对路径拒绝、行号计数、合法条目接受，
// 并对账 /builder（recordBuild 侧效：manifest 条目/path/result/计数）。
// 清单夹具生成于 <测试可执行目录>/manifest（跟随测试程序，与工作目录无关，
// 直接运行也不污染当前目录）；插件 so 符号链接进夹具目录满足相对路径契约。
#include <yomk/plugin_system/YomkPluginApi.h>
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

// 夹具：夹具目录内文件完整路径（清单/实例文件/符号链接统一走此路径）
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

// 夹具：空实例文件（内容透传不读）
static void touchInstanceFile(const std::string& name)
{
    std::ofstream f(fp(name));
}

// 夹具：符号链接插件 so 到夹具目录（清单 lib 段用相对路径引用）
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
    yomk::payload::Response resp = yomk::plugin().infoBuilder();
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
    check(yomk::server().srv<YomkPluginSystemBuilder>().create() == 0, "register YomkPluginSystemBuilder");

    namespace fs = std::filesystem;
    // 夹具目录每次运行清空重建，保证干净起点
    std::error_code ec;
    fs::remove_all(fixtureDir(), ec);
    fs::create_directories(fixtureDir(), ec);
    check(fs::is_directory(fixtureDir()), "fixture dir prepared under test executable");

    linkPlugin();
    touchInstanceFile("inst.txt");

    // ---------- A 路径预检：不存在/空串/目录/空白串 ----------
    expectNo(yomk::plugin().manifest("/nonexistent/no_such.yomk").build(), "manifest not found", "A1 nonexistent path rejects");
    expectNo(yomk::plugin().manifest("").build(), "manifest not found", "A2 empty path rejects");
    fs::create_directory(fp("adirectory"), ec);
    expectNo(yomk::plugin().manifest(fp("adirectory")).build(), "manifest not found", "A3 directory path rejects");
    expectNo(yomk::plugin().manifest("   ").build(), "manifest not found", "A4 whitespace-only path rejects");

    // ---------- B 首行格式标识 ----------
    writeManifest("m-empty.yomk", "");
    expectNo(yomk::plugin().manifest(fp("m-empty.yomk")).build(), "format marker not found", "B1 empty file rejects marker");
    writeManifest("m-wrong.yomk", "#! other_format\n");
    expectNo(yomk::plugin().manifest(fp("m-wrong.yomk")).build(), "format marker not found", "B2 wrong marker rejects");
    writeManifest("m-pad.yomk", "  #! yomk_plugin_system\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-pad.yomk")).build(), "plugins:0 instances:0", "B3 leading-space marker accepts");
    writeManifest("m-trail.yomk", "#! yomk_plugin_system trailing\n");
    expectNo(yomk::plugin().manifest(fp("m-trail.yomk")).build(), "format marker not found", "B4 marker trailing content rejects");
    writeManifest("m-notfirst.yomk", "entry\n#! yomk_plugin_system\n");
    expectNo(yomk::plugin().manifest(fp("m-notfirst.yomk")).build(), "line 1: format marker not found", "B5 marker not first line");
    writeManifest("m-crlf.yomk", "#! yomk_plugin_system\r\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-crlf.yomk")).build(), "plugins:0 instances:0", "B6 marker line CRLF accepts");

    // ---------- C 注释与空行 ----------
    writeManifest("m-comments.yomk", "#! yomk_plugin_system\n# c1\n# c2\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-comments.yomk")).build(), "plugins:0 instances:0", "C1 all-comment manifest ok");
    writeManifest("m-blanks.yomk", "#! yomk_plugin_system\n\n   \n\t\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-blanks.yomk")).build(), "plugins:0 instances:0", "C2 blank/whitespace lines skip");
    writeManifest("m-inline.yomk", "#! yomk_plugin_system\ni-inline@libTestPlugin.so@inst.txt # cfg comment\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-inline.yomk")).build(), "plugins:1 instances:1", "C3 inline comment truncated");

    // ---------- D @ 切分与空段 ----------
    const std::string fmtErr = "expect instanceName@libRelPath@instanceFileRelPath";
    const std::string emptyErr = "empty segment";
    writeManifest("m-d1.yomk", "#! yomk_plugin_system\njustname\n");
    expectNo(yomk::plugin().manifest(fp("m-d1.yomk")).build(), fmtErr, "D1 no '@' rejects");
    writeManifest("m-d2.yomk", "#! yomk_plugin_system\na@b\n");
    expectNo(yomk::plugin().manifest(fp("m-d2.yomk")).build(), fmtErr, "D2 one '@' rejects");
    writeManifest("m-d3.yomk", "#! yomk_plugin_system\na@b@c@d\n");
    expectNo(yomk::plugin().manifest(fp("m-d3.yomk")).build(), fmtErr, "D3 three '@' rejects");
    writeManifest("m-d4.yomk", "#! yomk_plugin_system\n@lib@file\n");
    expectNo(yomk::plugin().manifest(fp("m-d4.yomk")).build(), emptyErr, "D4 empty instance name rejects");
    writeManifest("m-d5.yomk", "#! yomk_plugin_system\na@@file\n");
    expectNo(yomk::plugin().manifest(fp("m-d5.yomk")).build(), emptyErr, "D5 empty lib segment rejects");
    writeManifest("m-d6.yomk", "#! yomk_plugin_system\na@lib@\n");
    expectNo(yomk::plugin().manifest(fp("m-d6.yomk")).build(), emptyErr, "D6 empty file segment rejects");
    writeManifest("m-d7.yomk", "#! yomk_plugin_system\n@@\n");
    expectNo(yomk::plugin().manifest(fp("m-d7.yomk")).build(), emptyErr, "D7 all-empty segments reject");
    writeManifest("m-d8.yomk", "#! yomk_plugin_system\na@ \t @c\n");
    expectNo(yomk::plugin().manifest(fp("m-d8.yomk")).build(), emptyErr, "D8 whitespace-only segment rejects");

    // ---------- E 绝对路径拒绝 ----------
    writeManifest("m-e1.yomk", "#! yomk_plugin_system\na@/abs/lib.so@f\n");
    expectNo(yomk::plugin().manifest(fp("m-e1.yomk")).build(), "lib path must be relative", "E1 absolute lib path rejects");
    writeManifest("m-e2.yomk", "#! yomk_plugin_system\na@lib@/abs/f\n");
    expectNo(yomk::plugin().manifest(fp("m-e2.yomk")).build(), "instance file path must be relative", "E2 absolute file path rejects");

    // ---------- F 行号计数（注释/空行跳过后行号正确） ----------
    writeManifest("m-f2.yomk", "#! yomk_plugin_system\nbad\n");
    expectNo(yomk::plugin().manifest(fp("m-f2.yomk")).build(), "manifest line 2:", "F1 error reported on line 2");
    writeManifest("m-f3.yomk", "#! yomk_plugin_system\n# comment\nbad\n");
    expectNo(yomk::plugin().manifest(fp("m-f3.yomk")).build(), "manifest line 3:", "F2 error reported on line 3");
    writeManifest("m-f5.yomk", "#! yomk_plugin_system\n# c\n\n   \nbad\n");
    expectNo(yomk::plugin().manifest(fp("m-f5.yomk")).build(), "manifest line 5:", "F3 error on line 5 after skips");

    // ---------- G 合法条目接受（真实加载 TestPlugin） ----------
    writeManifest("m-g1.yomk", "#! yomk_plugin_system\ni-single@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-g1.yomk")).build(), "plugins:1 instances:1", "G1 single entry builds");
    writeManifest("m-g2.yomk", "#! yomk_plugin_system\ni-a@libTestPlugin.so@inst.txt\ni-b@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-g2.yomk")).build(), "plugins:1 instances:2", "G2 two entries same lib");
    writeManifest("m-g3.yomk", "#! yomk_plugin_system\n\t i-pad \t@\t libTestPlugin.so \t@\t inst.txt \t\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-g3.yomk")).build(), "plugins:1 instances:1", "G3 padded segments trim");
    writeManifest("m-g4.yomk", "#! yomk_plugin_system\ni-crlf@libTestPlugin.so@inst.txt\r\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-g4.yomk")).build(), "plugins:1 instances:1", "G4 entry line CRLF accepts");
    writeManifest("m-g5.yomk", "#! yomk_plugin_system\nmy inst@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plugin().manifest(fp("m-g5.yomk")).build(), "plugins:1 instances:1", "G5 space in instance name accepts");

    // ---------- H /builder 对账（recordBuild 侧效） ----------
    const yomk::payload::BuilderInfo dump = builderInfo();
    check(
        dump.path == fp("m-g5.yomk") && dump.result == "ok" && dump.plugins == 1 && dump.instances == 1,
        "H1 /builder shows manifest path and ok result");
    check(
        std::find(dump.manifest.begin(), dump.manifest.end(), "my inst@libTestPlugin.so@inst.txt") !=
            dump.manifest.end(),
        "H2 /builder keeps entry lines of latest build");

    writeManifest("m-hfail.yomk", "#! yomk_plugin_system\n@lib@file\n");
    expectNo(yomk::plugin().manifest(fp("m-hfail.yomk")).build(), emptyErr, "H3 failure build Failed");
    const yomk::payload::BuilderInfo hfail = builderInfo();
    check(
        hfail.path == fp("m-hfail.yomk") && hfail.result == "manifest line 2: empty segment" && hfail.plugins == 0 &&
            hfail.instances == 0 && hfail.manifest.empty(),
        "H4 /builder records failure result");

    writeManifest(
        "m-part.yomk",
        "#! yomk_plugin_system\ngood1@libTestPlugin.so@inst.txt\nbad2@libTestPlugin.so@missing_inst.txt\n");
    expectNo(yomk::plugin().manifest(fp("m-part.yomk")).build(), "manifest line 3: instance file not found", "H5 partial build fails");
    const yomk::payload::BuilderInfo partDump = builderInfo();
    check(
        partDump.path == fp("m-part.yomk") &&
            partDump.result == "manifest line 3: instance file not found: " + fp("missing_inst.txt") &&
            partDump.plugins == 1 && partDump.instances == 1,
        "H6 /builder records partial failure with partial counts");
    check(
        std::find(partDump.manifest.begin(), partDump.manifest.end(), "good1@libTestPlugin.so@inst.txt") !=
            partDump.manifest.end(),
        "H7 /builder keeps completed entry lines");

    // ---------- 清理现场 ----------
    check(isOk(yomk::plugin().unload("TestPlugin")), "cleanup: force_unload TestPlugin");
    yomk::payload::Response inst = yomk::plugin().infoInstances();
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
