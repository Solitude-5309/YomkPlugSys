#pragma once
#include <memory>
#include <string>
#include <vector>

#include "yomk/msg/YomkMsg.h"
#include "yomk/server/YomkServerPayload.h"

#include "YomkPluginInterface.h"
#include "YomkPluginPayload.h"

/*
 * YomkPluginSystem 扩展专属消息包（成员名均为 d，与 SDK 各 Yomk{Module}Msg.h
 * 同一设计）：经 YOMK_DEFINE_MSG 生成于 yomk::msgs 命名空间；数据结构体在
 * yomk::payload（业务字段访问），消息包类在 yomk::msgs（跨请求传递）。
 * 数组类直接以 std::vector 为 DataType，同框架内置 StringArray 风格。
 * 注：框架宏生成 MsgName(const DataType&) 构造，消息包类名与数据结构体同名会与拷贝
 * 构造签名冲突，故同名者类名加 Msg 后缀定义，再以命名空间内 using 别名把调用侧类型名
 * 映射回业务名（对齐 SDK 消息包以业务名调用的惯例，如 unpack<yomk::msgs::String>）；
 * 运行时消息名（kName，内省展示用）相应带 Msg 后缀。
 */
// clang-format off
YOMK_DEFINE_MSG(yomk::payload::PluginPath, PluginPathMsg, d)
YOMK_DEFINE_MSG(yomk::payload::PluginMeta, PluginMetaMsg, d)
YOMK_DEFINE_MSG(yomk::payload::CreateReq, CreateReqMsg, d)
YOMK_DEFINE_MSG(yomk::payload::DestroyReq, DestroyReqMsg, d)
YOMK_DEFINE_MSG(yomk::payload::BuildReq, BuildReqMsg, d)
YOMK_DEFINE_MSG(yomk::payload::InstanceInfo, InstanceInfoMsg, d)
YOMK_DEFINE_MSG(yomk::payload::BuilderInfo, BuilderInfoMsg, d)
YOMK_DEFINE_MSG(std::vector<yomk::payload::PluginMeta>, PluginMetaArray, d)
YOMK_DEFINE_MSG(std::vector<yomk::payload::InstanceInfo>, InstanceInfoArray, d)

/* shared_ptr 经消息包在进程内直接持有传递（无序列化，仅限同进程内请求，不可跨进程） */
YOMK_DEFINE_MSG(std::shared_ptr<YomkPluginInterface>, PluginInstance, d)

/* 消息包业务名映射（yomk::msgs::PluginPath 即 PluginPathMsg）：Msg 后缀仅是同名冲突
 * 下的定义侧产物，调用侧（YomkMkPtr/unpack）经别名以业务名访问，对齐 SDK 惯例 */
namespace yomk::msgs
{
using PluginPath = PluginPathMsg;
using PluginMeta = PluginMetaMsg;
using CreateReq = CreateReqMsg;
using DestroyReq = DestroyReqMsg;
using BuildReq = BuildReqMsg;
using InstanceInfo = InstanceInfoMsg;
using BuilderInfo = BuilderInfoMsg;
}  // namespace yomk::msgs
// clang-format on

/* 请求侧消息包类型校验辅助：unpack 未命中（pkg 为空或类型不匹配）时构造失败响应。
 * YomkServer 3.x 的 unpack 不抛异常返回 nullptr（2.x 宏为抛异常转 eNo），
 * 服务函数须判空并用本辅助返回失败，具体值原则报出期望与实际类型 */
inline yomk::payload::Response invalidReqMsg(const YomkMsgPtr& pkg, const char* expectType)
{
    return {yomk::payload::ResStatus::Failed,
            std::string("invalid request msg type: expect ") + expectType + ", got " + yomk::msgs::typeOf(pkg)};
}
