#pragma once

#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "Common.h"
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
class FSendTaskQueueBasicTest;
class FSendTaskQueueSubmitTest;
class FSendTaskQueueCapacityTest;
class FSendTaskQueueShutdownTest;
class FSenderTaskQueueIntegrationTest;
#endif

namespace aliyun
{
namespace sls
{

// 前向声明
class FPersistentFile;

/**
 * @brief 发送任务
 */
struct FSendTask
{
    FInnerProducerConfigPtr Config;
    FCompressedLogBuffer Buffer; // 压缩后的日志数据（包含 Data, RawSize, CompressType, LogCount）
    uint32 CreateTime = 0;       // 创建时间
    int32 RetryCount = 0;        // 重试次数

    // 持久化相关（新增）
    TWeakPtr<FPersistentFile> FilePtr; // 使用 WeakPtr 避免生命周期问题
    int32 BlockIndex = -1;             // -1 表示非持久化任务（降级模式）
    TAtomic<bool> bCompletionReported{false};

    // 便捷访问器
    TArray<uint8>& Data()
    {
        return Buffer.Data;
    }
    const TArray<uint8>& Data() const
    {
        return Buffer.Data;
    }
    int64 RawSize() const
    {
        return Buffer.RawSize;
    }
    const FString& CompressType() const
    {
        return Buffer.CompressType;
    }
    int32 LogCount() const
    {
        return Buffer.LogCount;
    }

    /** 是否为持久化任务 */
    bool IsPersisted() const
    {
        return BlockIndex >= 0;
    }

    bool TryMarkCompletionReported()
    {
        bool bExpected = false;
        return bCompletionReported.CompareExchange(bExpected, true);
    }

    FSendTask() = default;
};

/**
 * @brief 发送任务队列
 *
 * 合并 SendQueue 和 RetryQueue，负责：
 * - 接收来自 Flusher 的新任务
 * - 管理重试任务的延迟调度
 * - 向 Sender 提供就绪任务
 *
 * 线程安全：
 * - Submit/Requeue 可从任意线程调用
 * - Dequeue 应在 Sender 线程调用
 */
class FSendTaskQueue
{
#if WITH_DEV_AUTOMATION_TESTS
    friend class ::FSendTaskQueueBasicTest;
    friend class ::FSendTaskQueueSubmitTest;
    friend class ::FSendTaskQueueCapacityTest;
    friend class ::FSendTaskQueueShutdownTest;
    friend class ::FSenderTaskQueueIntegrationTest;
#endif

public:
    /**
     * @brief 构造函数
     * @param InMaxQueueSize 最大队列大小
     */
    explicit FSendTaskQueue(int32 InMaxQueueSize = 4096);

    ~FSendTaskQueue();

    // 禁止拷贝
    FSendTaskQueue(const FSendTaskQueue&) = delete;
    FSendTaskQueue& operator=(const FSendTaskQueue&) = delete;

    /**
     * @brief 提交新任务（线程安全）
     * @param Task 任务
     * @return true 成功，false 队列满
     */
    bool Submit(TSharedPtr<FSendTask> Task);

    /**
     * @brief 重新入队（用于重试，线程安全）
     * @param Task 任务
     * @param DelayMs 延迟时间（毫秒）
     */
    void Requeue(TSharedPtr<FSendTask> Task, int32 DelayMs);

    /**
     * @brief 取出一个就绪任务（阻塞式）
     * @param OutTask 输出任务
     * @param TimeoutMs 超时时间（毫秒），0 表示立即返回
     * @return true 成功取到任务，false 超时或已关闭
     */
    bool Dequeue(TSharedPtr<FSendTask>& OutTask, uint32 TimeoutMs = 100);

    /**
     * @brief 取出所有剩余任务（关闭时使用）
     * @param OutTasks 输出任务列表
     */
    void DrainAll(TArray<TSharedPtr<FSendTask>>& OutTasks);

    /**
     * @brief 关闭队列
     */
    void Shutdown();

    bool IsShutdown() const
    {
        return bShutdown.Load();
    }

private:
    void ProcessRetryQueue();

    // 主队列（立即就绪的任务）
    TQueue<TSharedPtr<FSendTask>, EQueueMode::Mpsc> ReadyQueue;

    // 重试队列（延迟任务，最小堆）
    struct FDelayedTask
    {
        TSharedPtr<FSendTask> Task;
        double ReadyTime;

        bool operator<(const FDelayedTask& Other) const
        {
            return ReadyTime < Other.ReadyTime;
        }
    };
    TArray<FDelayedTask> DelayHeap;
    FCriticalSection DelayLock;

    TAtomic<int32> QueueSize{0};
    int32 MaxQueueSize;

    TAtomic<bool> bShutdown{false};
    FEventRef WakeEvent{EEventMode::ManualReset};
};

} // namespace sls
} // namespace aliyun
