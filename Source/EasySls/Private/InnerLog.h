#pragma once

#include "CoreMinimal.h"

// 声明日志类别
DECLARE_LOG_CATEGORY_EXTERN(LogEasySls, Log, All);

/**
 * @brief 内部日志宏
 *
 * 仅供 EasySls 插件内部使用，不暴露给外部用户
 *
 * 日志级别控制：
 * - 配置文件: [Core.Log] LogEasySls=Verbose
 * - 命令行: -LogCmds="LogEasySls Verbose"
 * - 控制台: Log LogEasySls Verbose
 */

// Error - 错误
#define SLS_LOG_ERROR(Format, ...) UE_LOG(LogEasySls, Error, TEXT(Format), ##__VA_ARGS__)

// Warning - 警告
#define SLS_LOG_WARNING(Format, ...) UE_LOG(LogEasySls, Warning, TEXT(Format), ##__VA_ARGS__)

// Info - 普通信息
#define SLS_LOG_INFO(Format, ...) UE_LOG(LogEasySls, Display, TEXT(Format), ##__VA_ARGS__)

// Debug - 调试信息（需启用 Verbose）
#define SLS_LOG_DEBUG(Format, ...) UE_LOG(LogEasySls, Verbose, TEXT(Format), ##__VA_ARGS__)

// Trace - 追踪信息（需启用 VeryVerbose）
#define SLS_LOG_TRACE(Format, ...) UE_LOG(LogEasySls, VeryVerbose, TEXT(Format), ##__VA_ARGS__)
