#pragma once
#include <yomk/server/YomkServerApi.h>

#include "plug_sys/YomkPlugSysMsg.h"

using namespace yomk;

// YomkManifestParser:清单解析服务 (/YomkManifestParser).
// 设计取舍:
// 1. 解析与编排分离:清单语法、块/键校验与错误行号全部集中在本服务,Builder 只消费
//    ManifestParseResult（清单目录 + 实例块表）,不再关心文本格式;
// 2. 语法扩展只改本服务:新增键/新块类型/新格式线时,对外契约（ManifestEntry）可加字段,
//    Builder 与调用方按需读取,解析细节不外泄;
// 3. 与 Loader/Manager 同为内部服务,由 Builder init() 注册、deinit 逆序注销,不随安装导出.
class YomkManifestParser : public YomkService
{
public:
    YomkManifestParser(YomkServer* server);
    ~YomkManifestParser() override = default;
    int init() override;

private:
    // 请求接口:注释给出 入参消息 -> 响应 data 消息
    yomk::payload::Response parse(YomkMsgPtr pkg);   // BuildReq manifestPath -> ManifestParseResult
};
