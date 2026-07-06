#include "SendTaskQueue.h"

namespace aliyun
{
namespace sls
{

FSendTaskQueue::FSendTaskQueue(int32 InMaxQueueSize) : MaxQueueSize(InMaxQueueSize) {}

FSendTaskQueue::~FSendTaskQueue()
{
    Shutdown();
}

bool FSendTaskQueue::Submit(TSharedPtr<FSendTask> Task)
{
    if (bShutdown.Load() || QueueSize.Load() >= MaxQueueSize)
    {
        return false;
    }

    QueueSize.IncrementExchange();
    ReadyQueue.Enqueue(Task);
    WakeEvent->Trigger();
    return true;
}

void FSendTaskQueue::Requeue(TSharedPtr<FSendTask> Task, int32 DelayMs)
{
    if (bShutdown.Load())
    {
        return;
    }

    FScopeLock ScopeLock(&DelayLock);

    FDelayedTask Item;
    Item.Task = Task;
    Item.ReadyTime = FPlatformTime::Seconds() + DelayMs / 1000.0;
    DelayHeap.HeapPush(Item);

    WakeEvent->Trigger();
}

bool FSendTaskQueue::Dequeue(TSharedPtr<FSendTask>& OutTask, uint32 TimeoutMs)
{
    // 先处理到期的重试任务
    ProcessRetryQueue();

    // 尝试从就绪队列取任务
    if (ReadyQueue.Dequeue(OutTask))
    {
        QueueSize.DecrementExchange();
        return true;
    }

    // 队列空，等待
    if (TimeoutMs > 0 && !bShutdown.Load())
    {
        // 计算等待时间：取 TimeoutMs 和下一个重试任务的等待时间的较小值
        uint32 WaitMs = TimeoutMs;
        {
            FScopeLock ScopeLock(&DelayLock);
            if (DelayHeap.Num() > 0)
            {
                double NextReady = DelayHeap.HeapTop().ReadyTime;
                double WaitSec = NextReady - FPlatformTime::Seconds();
                if (WaitSec > 0)
                {
                    WaitMs = FMath::Min(TimeoutMs, static_cast<uint32>(WaitSec * 1000));
                }
                else
                {
                    WaitMs = 0; // 已有到期任务，不等待
                }
            }
        }

        if (WaitMs > 0)
        {
            WakeEvent->Wait(WaitMs);
            WakeEvent->Reset();
        }

        // 再次处理重试队列
        ProcessRetryQueue();

        // 再次尝试取任务
        if (ReadyQueue.Dequeue(OutTask))
        {
            QueueSize.DecrementExchange();
            return true;
        }
    }

    return false;
}

void FSendTaskQueue::ProcessRetryQueue()
{
    FScopeLock ScopeLock(&DelayLock);

    const double Now = FPlatformTime::Seconds();
    while (DelayHeap.Num() > 0 && DelayHeap.HeapTop().ReadyTime <= Now)
    {
        FDelayedTask Item;
        DelayHeap.HeapPop(Item);
        // 重试任务已在队列计数中，直接入队
        ReadyQueue.Enqueue(MoveTemp(Item.Task));
    }
}

void FSendTaskQueue::DrainAll(TArray<TSharedPtr<FSendTask>>& OutTasks)
{
    // 取出就绪队列的所有任务
    TSharedPtr<FSendTask> Task;
    while (ReadyQueue.Dequeue(Task))
    {
        OutTasks.Add(Task);
    }

    // 取出重试队列的所有任务
    FScopeLock ScopeLock(&DelayLock);
    while (DelayHeap.Num() > 0)
    {
        FDelayedTask Item;
        DelayHeap.HeapPop(Item);
        OutTasks.Add(MoveTemp(Item.Task));
    }

    QueueSize.Store(0);
}

void FSendTaskQueue::Shutdown()
{
    bShutdown.Store(true);
    WakeEvent->Trigger();
}

} // namespace sls
} // namespace aliyun
