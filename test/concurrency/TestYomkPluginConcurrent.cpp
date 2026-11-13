// TestYomkPluginConcurrent：并发正确性测试（闭环7）
// 以多线程并发触达全部竞争窗口：create/destroy 不同名计数守恒、并发 build 幂等
// 收敛、unload vs create 竞争终态、churn 期间内省快照自洽。
// 规模为数百次交错（总操作 <1000），正确性看交错多样性而非次数。
// 装置 TSan-clean：线程内只写原子计数，join 后主线程统一断言。
// 夹具模式同 TestYomkPluginBuildLifecycle：<测试可执行目录>/manifest。
#include <yomk/plug_sys/YomkPlugSysApi.h>
#include <yomk/server/YomkServerApi.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
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
    check(!isOk(r) && r.msg.find(substr) != std::string::npos, desc);
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
    yomk::payload::Response resp = yomk::plug_sys().instances();
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
    yomk::payload::Response resp = yomk::plug_sys().plugins();
    if (resp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
        out = arr->d;
    }
    return out;
}

// 白盒：Manager create_instance 直调
static yomk::payload::Response createInst(const std::string& name)
{
    yomk::payload::CreateReq req;
    req.pluginName = "TestPlugin";
    req.instanceName = name;
    req.instanceCfg = fp("inst.txt");
    return yomk::server().url("/YomkPluginManager/create_instance").msg(yomk::msgs::CreateReq::shared(req)).request();
}

// 白盒：Manager destroy_instance 直调
static yomk::payload::Response destroyInst(const std::string& name)
{
    yomk::payload::DestroyReq req;
    req.pluginName = "TestPlugin";
    req.instanceName = name;
    return yomk::server().url("/YomkPluginManager/destroy_instance").msg(yomk::msgs::DestroyReq::shared(req)).request();
}

// C3 create 结果文案白名单：ok 或竞争窗口的合法失败文案（Manager 层 + Loader 层）
static bool c3MsgAllowed(const yomk::payload::Response& r)
{
    if (r.status == yomk::payload::ResStatus::Succeeded)
    {
        return true;
    }
    return r.msg.find("plugin unloaded during create") != std::string::npos ||
           r.msg.find("duplicate instance name") != std::string::npos ||
           r.msg.find("plugin not loaded") != std::string::npos ||
           r.msg.find("plugin not loaded") != std::string::npos;
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

    fs::create_symlink(pluginLibPath(), fp("libTestPlugin.so"), ec);
    {
        std::ofstream f(fp("inst.txt"));
    }

    // ---------- C0 基线：插件加载态 ----------
    writeManifest("c0.yomk", "#! yomk_plug_sys\nc0-i1@libTestPlugin.so@inst.txt\n");
    expectOkSummary(yomk::plug_sys().manifest(fp("c0.yomk")).build(), "plugins:1 instances:1", "C0 build baseline ok");
    check(isOk(destroyInst("c0-i1")), "C0 destroy baseline instance (plugin stays loaded)");

    // ---------- C1 并发 create/destroy 计数守恒：8 线程 x 50 ----------
    std::atomic<int> c1CreateOk{0};
    std::atomic<int> c1CreateFail{0};
    std::atomic<int> c1DestroyOk{0};
    std::atomic<int> c1DestroyFail{0};
    {
        std::vector<std::thread> pool;
        for (int t = 0; t < 8; ++t)
        {
            pool.emplace_back(
                [&, t]()
                {
                    for (int i = 0; i < 50; ++i)
                    {
                        const std::string name = "ci-" + std::to_string(t) + "-" + std::to_string(i);
                        if (isOk(createInst(name)))
                        {
                            ++c1CreateOk;
                        }
                        else
                        {
                            ++c1CreateFail;
                        }
                    }
                });
        }
        for (auto& th : pool)
        {
            th.join();
        }
    }
    check(c1CreateOk.load() == 400 && c1CreateFail.load() == 0, "C1 concurrent create 400 ok, 0 fail");
    check(listInstances().size() == 400, "C1 instances count 400 (conserved)");
    {
        std::vector<std::thread> pool;
        for (int t = 0; t < 8; ++t)
        {
            pool.emplace_back(
                [&, t]()
                {
                    for (int i = 0; i < 50; ++i)
                    {
                        const std::string name = "ci-" + std::to_string(t) + "-" + std::to_string(i);
                        if (isOk(destroyInst(name)))
                        {
                            ++c1DestroyOk;
                        }
                        else
                        {
                            ++c1DestroyFail;
                        }
                    }
                });
        }
        for (auto& th : pool)
        {
            th.join();
        }
    }
    check(c1DestroyOk.load() == 400 && c1DestroyFail.load() == 0, "C1 concurrent destroy 400 ok, 0 fail");
    check(listInstances().empty(), "C1 instances conserved back to zero");

    // ---------- C2 并发 build 幂等收敛：4 线程并发同一清单 ----------
    check(isOk(yomk::plug_sys().plg_name("TestPlugin").unload()), "C2 preload unload for build race");
    writeManifest("c2.yomk", "#! yomk_plug_sys\nc2-i1@libTestPlugin.so@inst.txt\n");
    std::atomic<int> c2Ok{0};
    std::atomic<int> c2MsgBad{0};
    {
        std::vector<std::thread> pool;
        for (int t = 0; t < 4; ++t)
        {
            pool.emplace_back(
                [&]()
                {
                    yomk::payload::Response r = yomk::plug_sys().manifest(fp("c2.yomk")).build();
                    if (isOk(r))
                    {
                        ++c2Ok;
                    }
                    else if (
                        r.msg.find("load failed: plugin already loaded: TestPlugin") == std::string::npos &&
                        r.msg.find("create failed: duplicate instance name: c2-i1") == std::string::npos)
                    {
                        // 两条合法竞争文案：load 判重失败 / 预查命中后 create 查重失败
                        ++c2MsgBad;
                    }
                });
        }
        for (auto& th : pool)
        {
            th.join();
        }
    }
    check(c2Ok.load() == 1, "C2 exactly one concurrent build ok");
    check(c2MsgBad.load() == 0, "C2 rejected builds all report already-loaded");
    check(listPlugins().size() == 1 && listInstances().size() == 1, "C2 terminal state plugins:1 instances:1");
    check(isOk(destroyInst("c2-i1")), "C2 destroy built instance (plugin stays loaded)");

    // ---------- C3 并发 unload vs create：竞争终态必空 ----------
    std::atomic<bool> c3Stop{false};
    std::atomic<int> c3Created{0};
    std::atomic<int> c3MsgBad{0};
    {
        std::vector<std::thread> pool;
        for (int t = 0; t < 4; ++t)
        {
            pool.emplace_back(
                [&, t]()
                {
                    int i = 0;
                    while (!c3Stop.load())
                    {
                        yomk::payload::Response r = createInst("c3-" + std::to_string(t) + "-" + std::to_string(i++));
                        if (!c3MsgAllowed(r))
                        {
                            ++c3MsgBad;
                        }
                        if (isOk(r))
                        {
                            ++c3Created;
                        }
                    }
                });
        }
        pool.emplace_back(
            [&]()
            {
                // 等待存在存活实例再强卸（上限 200ms），覆盖整组移出路径
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
                while (c3Created.load() < 5 && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::yield();
                }
                // force_unload 锁外段窗口允许 create 继续登记（unload 以 instances still
                // alive 失败属合法竞争结果），重试强卸直至收敛：成功 UNLOAD 后 plugins 必空，
                // 此后 create 全被拒，终态必空
                for (int retry = 0; retry < 200; ++retry)
                {
                    if (isOk(yomk::plug_sys().plg_name("TestPlugin").unload()))
                    {
                        break;
                    }
                    std::this_thread::yield();
                }
                c3Stop.store(true);
            });
        for (auto& th : pool)
        {
            th.join();
        }
    }
    check(c3MsgBad.load() == 0, "C3 create results all in allowed message set");
    check(listInstances().empty(), "C3 terminal instances empty after unload race");
    check(listPlugins().empty(), "C3 terminal plugins empty after unload race");
    expectNo(yomk::plug_sys().plg_name("TestPlugin").unload(), "plugin not loaded: TestPlugin", "C3 double unload rejects");

    // ---------- C4 并发内省 churn 自洽：4 churn + 2 内省 ----------
    // 前置：C3 结束插件已卸载，重建加载态（churn 需要插件在库中）
    writeManifest("c4pre.yomk", "#! yomk_plug_sys\nc4pre-i1@libTestPlugin.so@inst.txt\n");
    yomk::plug_sys().manifest(fp("c4pre.yomk")).build();
    destroyInst("c4pre-i1");

    std::atomic<bool> c4Done{false};
    std::atomic<int> c4CreateOk{0};
    std::atomic<int> c4CreateFail{0};
    std::atomic<int> c4DestroyOk{0};
    std::atomic<int> c4FacadeBad{0};
    {
        std::vector<std::thread> pool;
        for (int t = 0; t < 4; ++t)
        {
            pool.emplace_back(
                [&, t]()
                {
                    for (int i = 0; i < 50; ++i)
                    {
                        const std::string name = "c4-" + std::to_string(t) + "-" + std::to_string(i);
                        if (isOk(createInst(name)))
                        {
                            ++c4CreateOk;
                        }
                        else
                        {
                            ++c4CreateFail;
                        }
                        if (isOk(destroyInst(name)))
                        {
                            ++c4DestroyOk;
                        }
                    }
                });
        }
        for (int t = 0; t < 2; ++t)
        {
            pool.emplace_back(
                [&]()
                {
                    while (!c4Done.load())
                    {
                        // 门面内省三连：churn 期间恒 Succeeded
                        if (!isOk(yomk::plug_sys().plugins()) || !isOk(yomk::plug_sys().instances()) ||
                            !isOk(yomk::plug_sys().builder()))
                        {
                            ++c4FacadeBad;
                        }
                        std::this_thread::yield();
                    }
                });
        }
        // 先等 churn 线程结束，再停内省线程
        for (size_t i = 0; i < pool.size() - 2; ++i)
        {
            pool[i].join();
        }
        c4Done.store(true);
        for (size_t i = pool.size() - 2; i < pool.size(); ++i)
        {
            pool[i].join();
        }
    }
    check(
        c4CreateOk.load() == 200 && c4CreateFail.load() == 0 && c4DestroyOk.load() == 200,
        "C4 churn 200 create/destroy all ok");
    check(c4FacadeBad.load() == 0, "C4 facade introspection all ok during churn");
    check(listInstances().empty(), "C4 terminal instances empty after churn");

    // ---------- 收尾终态清零 ----------
    check(isOk(yomk::plug_sys().plg_name("TestPlugin").unload()), "cleanup: force_unload TestPlugin");
    check(listPlugins().empty() && listInstances().empty(), "cleanup: plugins and instances empty");

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
