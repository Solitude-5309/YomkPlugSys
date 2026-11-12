/*
 * TestYomkPluginFacade：门面链式域入口契约冒烟测试
 * 覆盖：门面服务注册、/version 契约与链式版本查询输出、
 * 空态内省契约（INFO_BUILDER 空态默认值、INFO_PLUGIN_BY/INFO_INSTANCES_FILE 未知实例拒绝、INFO_PLUGINS/INFO_INSTANCES 空数组）、
 * BUILD 不存在清单失败路径。
 */
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
    /* 门面服务：Loader/Manager 由 Builder::init 内部注册，用户仅注册本服务 */
    check(yomk::server().srv<YomkPluginSystemBuilder>().create() == 0, "register YomkPluginSystemBuilder");

    /* ---------- 版本契约 ---------- */
    yomk::payload::Response resp = yomk::server().url("/YomkPluginSystemBuilder/version").msg(nullptr).request();
    check(isOk(resp), "/version returns eOk");
    const std::string ver = respString(resp);
    check(ver.rfind("YomkPluginSystem v", 0) == 0, "/version prefix 'YomkPluginSystem v'");
    /* 链式查询 /version 并解包打印（成功走 info，输出人工核对） */
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

    /* ---------- 空态内省契约 ---------- */
    resp = yomk::plugin().infoBuilder();
    check(isOk(resp), "INFO_BUILDER returns eOk");
    auto info = yomk::msgs::unpack<yomk::msgs::BuilderInfo>(resp.data);
    check(info != nullptr, "INFO_BUILDER unpacks BuilderInfo");
    check(
        info && info->d.path.empty() && info->d.result.empty() && info->d.plugins == 0 && info->d.instances == 0 &&
            info->d.manifest.empty(),
        "INFO_BUILDER empty-state defaults (empty path/result, zero counts, no manifest lines)");

    resp = yomk::plugin().infoPluginBy("no-inst");
    check(
        isNo(resp) && resp.msg == "instance not found: no-inst",
        "INFO_PLUGIN_BY empty-state rejects unknown instance");

    resp = yomk::plugin().infoInstancesFile("no-inst");
    check(
        isNo(resp) && resp.msg == "instance not found: no-inst",
        "INFO_INSTANCES_FILE empty-state rejects unknown instance");

    resp = yomk::plugin().infoPlugins();
    bool pluginsEmpty = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::PluginMetaArray>(resp.data);
        pluginsEmpty = arr->d.empty();
    }
    check(pluginsEmpty, "INFO_PLUGINS eOk + empty array");

    resp = yomk::plugin().infoInstances();
    bool instEmpty = false;
    if (isOk(resp))
    {
        auto arr = yomk::msgs::unpack<yomk::msgs::InstanceInfoArray>(resp.data);
        instEmpty = arr->d.empty();
    }
    check(instEmpty, "INFO_INSTANCES eOk + empty array");

    /* ---------- BUILD 失败路径 ---------- */
    resp = yomk::plugin().build("/nonexistent/no_such.yomk");
    check(isNo(resp), "BUILD nonexistent manifest returns eNo");
    check(resp.msg.find("manifest not found") != std::string::npos, "BUILD error msg 'manifest not found'");

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
