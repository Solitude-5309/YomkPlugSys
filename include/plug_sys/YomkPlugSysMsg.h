#pragma once
#include <memory>
#include <string>
#include <vector>

#include "yomk/msg/YomkMsg.h"
#include "yomk/server/YomkServerPayload.h"

#include "YomkPluginInterface.h"
#include "YomkPlugSysPayload.h"

// YomkPlugSys 扩展专属消息包（成员名均为 d,与 SDK 各 Yomk{Module}Msg.h 同一设计）.
// 设计取舍:
// 1. 消息包类名与载荷结构体同名:二者分处 yomk::msgs::plug_sys 与 yomk::payload::plug_sys,宏生成的构造形参
//    写全限定载荷类型,与拷贝构造签名不同,故不冲突;运行时消息名 kName 即业务名;
// 2. 数组消息直接以 std::vector 为 DataType,同框架内置 StringArray 风格;
// 3. PluginInstance 经 shared_ptr 在进程内直接传递,无序列化,仅限同进程请求.
// clang-format off
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::PluginLoadReq, PluginLoadReq, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::PluginInfo, PluginInfo, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::CreateReq, CreateReq, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::DestroyReq, DestroyReq, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::BuildReq, BuildReq, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::InstanceInfo, InstanceInfo, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::ManifestEntry, ManifestEntry, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::ManifestParseResult, ManifestParseResult, d)
YOMK_DEFINE_MSG_NS(plug_sys, yomk::payload::plug_sys::BuilderInfo, BuilderInfo, d)
YOMK_DEFINE_MSG_NS(plug_sys, std::vector<yomk::payload::plug_sys::PluginInfo>, PluginInfoList, d)
YOMK_DEFINE_MSG_NS(plug_sys, std::vector<yomk::payload::plug_sys::InstanceInfo>, InstanceInfoList, d)

YOMK_DEFINE_MSG_NS(plug_sys, std::shared_ptr<YomkPluginInterface>, PluginInstance, d)
// clang-format on
