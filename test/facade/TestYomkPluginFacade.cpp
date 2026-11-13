// TestYomkPluginFacade：门面链式域入口契约冒烟测试
// 覆盖门面服务注册、/version 契约与链式版本查询、空态内省默认值与未知实例拒绝、BUILD 不存在清单失败路径。
#include <yomk/plugin_system/YomkPluginApi.h>
#include <yomk/server/YomkServerApi.h>

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

int main()
{
    yomk::server().init();
    // 门面服务：Loader/Manager 由 Builder::init 内部注册，用户仅注册本服务
    check(yomk::server().srv<YomkPluginSystemBuilder>().create() == 0, "register YomkPluginSystemBuilder");

    // ---------- 版本契约 ----------
    yomk::payload::Response resp = yomk::server().url("/YomkPluginSystemBuilder/version").msg(nullptr).request();
    check(isOk(resp), "/version returns Succeeded");
    const std::string ver = respString(resp);
    check(ver.rfind("YomkPluginSystem v", 0) == 0, "/version prefix 'YomkPluginSystem v'");
    // 链式查询 /version 并解包打印（成功走 info，输出人工核对）
    {
        auto verResp = yomk::plugin().version();
        if (verResp.status == yomk::payload::ResStatus::Succeeded)
        {
            auto ver = yomk::msgs::unpack<yomk::msgs::String>(verResp.data);
            if (ver)
            {
                yomk::log().tag("YomkPluginSystem").info(ver->d);
            }
        }
        else
        {
            yomk::log().tag("YomkPluginSystem").error("getVersion failed: ", verResp.msg);
        }
    }

    // ---------- 空态内省契约 ----------
    resp = yomk::plugin().builder();
    check(isOk(resp), "builder() returns Succeeded");
    auto info = yomk::msgs::unpack<yomk::msgs::BuilderInfo>(resp.data);
    check(info != nullptr, "builder() unpacks BuilderInfo");
    check(
        info && info->d.path.empty() && info->d.result.empty() && info->d.plugins == 0 && info->d.instances == 0 &&
            info->d.manifest.empty(),
        "builder() empty-state defaults (empty path/result, zero counts, no manifest lines)");

    resp = yomk::plugin().inst_name("no-inst").get_plg_info();
    check(
        isNo(resp) && resp.msg == "instance not found: no-inst",
        "inst_name(name).get_plg_info() empty-state rejects unknown instance");

    resp = yomk::plugin().inst_cfg("no-inst");
    check(
        isNo(resp) && resp.msg == "instance not found: no-inst",
        "inst_cfg(instanceName) empty-state rejects unknown instance");

    resp = yomk::plugin().plugins();
    bool pluginsEmpty = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
        pluginsEmpty = arr->d.empty();
    }
    check(pluginsEmpty, "plugins() Succeeded + empty array");

    resp = yomk::plugin().plg_name("no-plugin").get_info();
    check(
        isNo(resp) && resp.msg == "plugin not loaded: no-plugin",
        "plg_name(name).get_info() empty-state rejects unknown plugin");

    resp = yomk::plugin().instances();
    bool instEmpty = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
        instEmpty = arr->d.empty();
    }
    check(instEmpty, "instances() Succeeded + empty array");

    resp = yomk::plugin().plg_name("no-plugin").instances();
    check(
        isNo(resp) && resp.msg == "plugin not loaded: no-plugin",
        "plg_name(name).instances() empty-state rejects unknown plugin");

    // ---------- BUILD 失败路径 ----------
    resp = yomk::plugin().manifest("/nonexistent/no_such.yomk").build();
    check(isNo(resp), "BUILD nonexistent manifest returns Failed");
    check(resp.msg.find("manifest not found") != std::string::npos, "BUILD error msg 'manifest not found'");

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
