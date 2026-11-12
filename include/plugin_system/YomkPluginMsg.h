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
 * 消息包类名与数据结构体同名——二者分处 yomk::msgs / yomk::payload 不同命名空间
 * 互不冲突（宏生成构造的形参写明全限定 payload 类型，与拷贝构造签名不同）；
 * 运行时消息名（kName = 类名字符串化）即业务名。
 * 数组类直接以 std::vector 为 DataType，同框架内置 StringArray 风格。
 */
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

/* shared_ptr 经消息包在进程内直接持有传递（无序列化，仅限同进程内请求，不可跨进程） */
YOMK_DEFINE_MSG(std::shared_ptr<YomkPluginInterface>, PluginInstance, d)
// clang-format on
