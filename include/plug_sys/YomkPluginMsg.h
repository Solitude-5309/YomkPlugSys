#pragma once
#include <memory>
#include <string>
#include <vector>

#include "yomk/msg/YomkMsg.h"
#include "yomk/server/YomkServerPayload.h"

#include "YomkPluginInterface.h"
#include "YomkPluginPayload.h"

// YomkPlugSys 扩展专属消息包（成员名均为 d,与 SDK 各 Yomk{Module}Msg.h 同一设计）.
// 设计取舍:
// 1. 消息包类名与载荷结构体同名:二者分处 yomk::msgs 与 yomk::payload,宏生成的构造形参
//    写全限定载荷类型,与拷贝构造签名不同,故不冲突;运行时消息名 kName 即业务名;
// 2. 数组消息直接以 std::vector 为 DataType,同框架内置 StringArray 风格;
// 3. PluginInstance 经 shared_ptr 在进程内直接传递,无序列化,仅限同进程请求.
// clang-format off
YOMK_DEFINE_MSG(yomk::payload::PluginPath, PluginPath, d)
YOMK_DEFINE_MSG(yomk::payload::PluginMeta, PluginMeta, d)
YOMK_DEFINE_MSG(yomk::payload::CreateReq, CreateReq, d)
YOMK_DEFINE_MSG(yomk::payload::DestroyReq, DestroyReq, d)
YOMK_DEFINE_MSG(yomk::payload::BuildReq, BuildReq, d)
YOMK_DEFINE_MSG(yomk::payload::InstanceInfo, InstanceInfo, d)
YOMK_DEFINE_MSG(yomk::payload::BuilderInfo, BuilderInfo, d)
YOMK_DEFINE_MSG(std::vector<yomk::payload::PluginMeta>, PluginMetaArray, d)
YOMK_DEFINE_MSG(std::vector<yomk::payload::InstanceInfo>, InstanceInfoArray, d)

YOMK_DEFINE_MSG(std::shared_ptr<YomkPluginInterface>, PluginInstance, d)
// clang-format on
