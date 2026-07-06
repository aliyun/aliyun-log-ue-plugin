#pragma once

#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Common.h"
#include "CoreMinimal.h"
#include "LogItem.h"
#include "SlsConfig.h" // For FSlsSendCallback, FSlsSendResult

namespace aliyun
{
namespace sls
{

// 前向声明
class FSendTaskQueue;
class FPersistentManager;
class FPersistentFile;

// 结果码常量
namespace ESlsResult
{
constexpr int32 Ok = 0;
constexpr int32 Invalid = 1;
constexpr int32 WriteError = 2;
constexpr int32 DropError = 3;
constexpr int32 SerializeError = 4;
constexpr int32 CompressError = 5;
constexpr int32 SendNetworkError = 6;
constexpr int32 SendQuotaError = 7;
constexpr int32 SendUnauthorized = 8;
constexpr int32 SendServerError = 9;
constexpr int32 SendDiscardError = 10;
constexpr int32 SendTimeError = 11;
constexpr int32 MemoryReachLimit = 12; // 内存超限
constexpr int32 ParametersInvalid = 27;
constexpr int32 ProducerNotExist = 28;
} // namespace ESlsResult

/**
 * @brief 日志 Flusher
 *
 * 负责：
 * - 从日志缓冲区收集日志
 * - 聚合成 LogGroup
 * - 序列化 (Protobuf)
 * - 压缩 (LZ4)
 * - 提交到 SendTaskQueue
 */
class FFlusher : public FRunnable
{
public:
    /**
     * @brief 构造函数
     * @param InConfig 配置
     * @param InTaskQueue 发送任务队列
     * @param InDropCallback 日志丢弃时的回调（可选）
     * @param InPersistentManager 持久化管理器（可选，为 nullptr 时不持久化）
     */
    FFlusher(FInnerProducerConfigPtr InConfig,
             TSharedPtr<FSendTaskQueue> InTaskQueue,
             FSlsSendCallback InDropCallback = nullptr,
             TSharedPtr<FPersistentManager> InPersistentManager = nullptr);

    ~FFlusher();

    // 禁止拷贝
    FFlusher(const FFlusher&) = delete;
    FFlusher& operator=(const FFlusher&) = delete;

    /**
     * @brief 添加日志
     * @param Item 日志项
     * @param bFlush 是否立即刷新
     * @return 返回 ESlsAddLogResult
     */
    ESlsAddLogResult AddLog(const FLogItem& Item, bool bFlush = false);
    ESlsAddLogResult AddLog(FLogItem&& Item, bool bFlush = false);

    /**
     * @brief 启动 Flusher
     */
    void Start();

    /**
     * @brief 停止 Flusher（也是 FRunnable::Stop 的实现）
     */
    virtual void Stop() override;


    // FRunnable interface
    virtual bool Init() override
    {
        return true;
    }
    virtual uint32 Run() override;

private:
    void FlushLogs(TArray<FLogItem>& Logs, int64& CurrentBytes, bool bForceFlush);
    void ProcessLogGroup(TArray<FLogItem>&& Logs);

    FInnerProducerConfigPtr Config;
    TSharedPtr<FSendTaskQueue> TaskQueue;
    FSlsSendCallback DropCallback; // 日志丢弃时的回调

    // 持久化相关
    TSharedPtr<FPersistentManager> PersistentManager;
    TSharedPtr<FPersistentFile> ActiveFile;

    // 日志缓冲区
    TQueue<FLogItem, EQueueMode::Mpsc> LogBuffer;
    TAtomic<int32> LogCount{0};

    // 线程控制
    TAtomic<bool> bShutdown{false};
    FRunnableThread* WorkerThread = nullptr;
    FEventRef FlushEvent{EEventMode::ManualReset};

    // Pack ID
    FString PackIdPrefix;
    TAtomic<uint32> PackIndex{0};

    // 辅助方法
    void EnsureActiveFile();
    void CheckRotation();
};

} // namespace sls
} // namespace aliyun
