#include "Flusher.h"
#include "InnerLog.h"
#include "LogGroupBuilder.h"
#include "PersistentFile.h"
#include "PersistentManager.h"
#include "SendTaskQueue.h"
#include "ThirdParty.h"
#include "Utils.h"

namespace aliyun
{
namespace sls
{

FFlusher::FFlusher(FInnerProducerConfigPtr InConfig,
                   TSharedPtr<FSendTaskQueue> InTaskQueue,
                   FSlsSendCallback InDropCallback,
                   TSharedPtr<FPersistentManager> InPersistentManager)
    : Config(InConfig),
      TaskQueue(InTaskQueue),
      DropCallback(MoveTemp(InDropCallback)),
      PersistentManager(InPersistentManager),
      PackIdPrefix(FUtils::GeneratePackIdPrefix(InConfig->Config.Logstore, InConfig->Config.Source))
{
}

FFlusher::~FFlusher()
{
    Stop();
}

ESlsAddLogResult FFlusher::AddLog(const FLogItem& Item, bool bFlush)
{
    FLogItem Copy = Item;
    return AddLog(MoveTemp(Copy), bFlush);
}

ESlsAddLogResult FFlusher::AddLog(FLogItem&& Item, bool bFlush)
{
    // 检查内存限制
    if (Config->MemController->IsFull())
    {
        SLS_LOG_WARNING("Memory limit reached, dropping log");
        return ESlsAddLogResult::MemoryReachLimit;
    }

    // 入队
    LogBuffer.Enqueue(MoveTemp(Item));
    int32 Count = LogCount.IncrementExchange();

    // 检查是否需要触发刷新
    if (bFlush || (Count + 1) >= Config->Config.LogCountPerPackage)
    {
        SLS_LOG_DEBUG("Triggering flush: bFlush=%d, Count=%d", bFlush, Count + 1);
        FlushEvent->Trigger();
    }

    return ESlsAddLogResult::Ok;
}

void FFlusher::Start()
{
    SLS_LOG_INFO("Flusher starting...");
    bShutdown.Store(false);

    WorkerThread = FRunnableThread::Create(this, TEXT("SlsFlusher"), 0, TPri_BelowNormal);
    SLS_LOG_INFO("Flusher started, WorkerThread=%p", WorkerThread);
}

void FFlusher::Stop()
{
    SLS_LOG_INFO("Flusher stopping...");
    bShutdown.Store(true);
    FlushEvent->Trigger();

    if (WorkerThread)
    {
        SLS_LOG_DEBUG("Waiting for Flusher WorkerThread to complete...");
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }

    if (ActiveFile.IsValid() && PersistentManager.IsValid())
    {
        PersistentManager->FinishFile(ActiveFile);
        ActiveFile.Reset();
    }

    SLS_LOG_INFO("Flusher stopped");
}

uint32 FFlusher::Run()
{
    SLS_LOG_DEBUG("Flusher Run() loop started");
    TArray<FLogItem> Logs;
    int64 CurrentBytes = 0;

    while (!bShutdown.Load())
    {
        bool bTriggered = FlushEvent->Wait(Config->Config.FlushIntervalMs);
        FlushEvent->Reset();

        bool bForceFlush = !bTriggered;
        FlushLogs(Logs, CurrentBytes, bForceFlush);
    }

    SLS_LOG_DEBUG("Flusher shutdown, forcing final flush");
    FlushLogs(Logs, CurrentBytes, true);
    SLS_LOG_DEBUG("Flusher Run() loop exited");
    return 0;
}

void FFlusher::FlushLogs(TArray<FLogItem>& Logs, int64& CurrentBytes, bool bForceFlush)
{
    const int32 MaxBatch = Config->Config.LogCountPerPackage;
    const int64 MaxBytes = Config->Config.LogBytesPerPackage;

    FLogItem Item;
    while (LogBuffer.Dequeue(Item))
    {
        LogCount.DecrementExchange();
        CurrentBytes += Item.EstimateSize();
        Logs.Add(MoveTemp(Item));

        if (Logs.Num() >= MaxBatch || CurrentBytes >= MaxBytes)
        {
            ProcessLogGroup(MoveTemp(Logs));
            Logs.Empty();
            CurrentBytes = 0;
        }
    }

    if (bForceFlush && Logs.Num() > 0)
    {
        ProcessLogGroup(MoveTemp(Logs));
        Logs.Empty();
        CurrentBytes = 0;
    }
}

void FFlusher::ProcessLogGroup(TArray<FLogItem>&& Logs)
{
    if (Logs.Num() == 0)
    {
        return;
    }

    FLogGroupBuilder Builder;

    if (!Config->Config.Source.IsEmpty())
    {
        Builder.SetSource(Config->Config.Source);
    }
    if (!Config->Config.Topic.IsEmpty())
    {
        Builder.SetTopic(Config->Config.Topic);
    }
    for (const auto& Tag : Config->Config.Tags)
    {
        Builder.AddTag(Tag.Key, Tag.Value);
    }

    uint32 Index = PackIndex.IncrementExchange();
    Builder.SetPackId(FUtils::FormatPackId(PackIdPrefix, Index));

    for (auto& Log : Logs)
    {
        Builder.AddLog(MoveTemp(Log));
    }

    int64 OriginalSize = Builder.EstimatedSize();
    int32 LogCount = Logs.Num();

    TArray<uint8> Serialized = Builder.Serialize();
    if (Serialized.Num() == 0)
    {
        SLS_LOG_ERROR("Failed to serialize log group, dropping %d logs", LogCount);
        if (DropCallback)
        {
            DropCallback(FSlsSendResult(ESlsResult::SerializeError, TEXT("Serialize failed"), TEXT(""), LogCount));
        }
        return;
    }

    TSharedPtr<FSendTask> Task = MakeShared<FSendTask>();
    Task->Config = Config;
    Task->Buffer.RawSize = Serialized.Num();
    Task->Buffer.LogCount = LogCount;
    Task->CreateTime = FUtils::GetCurrentTimeSec();

    bool bIsLZ4 = (Config->Config.CompressType == ESlsCompressType::Lz4);
    if (bIsLZ4)
    {
        TArray<uint8> Compressed;
        if (!thirdparty::Lz4Compress(Serialized, Compressed) || (Compressed.Num() == 0 && Serialized.Num() > 0))
        {
            SLS_LOG_ERROR("Failed to compress log group, dropping %d logs", LogCount);
            if (DropCallback)
            {
                DropCallback(FSlsSendResult(ESlsResult::CompressError, TEXT("Compress failed"), TEXT(""), LogCount));
            }
            return;
        }
        Task->Buffer.Data = MoveTemp(Compressed);
        Task->Buffer.CompressType = SlsHeaders::XLogCompressTypeLz4;
    }
    else
    {
        Task->Buffer.Data = MoveTemp(Serialized);
    }

    // 持久化写入（如果启用）
    TSharedPtr<FPersistentFile> PersistedFile;
    if (PersistentManager.IsValid() && PersistentManager->IsEnabled())
    {
        EnsureActiveFile();

        if (ActiveFile.IsValid())
        {
            int32 BlockIndex = ActiveFile->AppendBlock(Task->Buffer.Data, Task->Buffer.RawSize, LogCount, bIsLZ4);
            if (BlockIndex >= 0)
            {
                Task->FilePtr = ActiveFile;
                Task->BlockIndex = BlockIndex;
                PersistedFile = ActiveFile;
                SLS_LOG_DEBUG("Persisted block %d with %d logs", BlockIndex, LogCount);

                CheckRotation();
            }
            else
            {
                SLS_LOG_WARNING("Failed to persist block, continuing without persistence");
            }
        }
    }

    if (!Config->MemController->TryAcquire(Task->Data().Num()))
    {
        SLS_LOG_WARNING("Memory limit exceeded, dropping %d logs (%lld bytes)", Task->LogCount(), Task->Data().Num());
        if (DropCallback)
        {
            DropCallback(FSlsSendResult(ESlsResult::MemoryReachLimit,
                                        TEXT("Memory limit exceeded"),
                                        TEXT(""),
                                        Task->LogCount()));
        }
        return;
    }

    if (!TaskQueue->Submit(Task))
    {
        Config->MemController->Release(Task->Data().Num());
        SLS_LOG_WARNING("Task queue is full, dropping %d logs", Task->LogCount());
        if (DropCallback)
        {
            DropCallback(FSlsSendResult(ESlsResult::DropError, TEXT("Task queue full"), TEXT(""), Task->LogCount()));
        }
        return;
    }

    // 提交成功后，追踪该 SendTask
    if (PersistedFile.IsValid())
    {
        PersistedFile->IncrementPendingSend();
    }

    // 轮询检查已完成发送的文件（每次产生 LogGroup 时检查一次）
    if (PersistentManager.IsValid())
    {
        PersistentManager->CheckSendingFiles();
    }

    SLS_LOG_DEBUG("Submitted log group with %d logs, %lld bytes, persisted=%d",
                  Task->LogCount(),
                  Task->Data().Num(),
                  Task->IsPersisted() ? 1 : 0);
}

void FFlusher::EnsureActiveFile()
{
    if (!PersistentManager.IsValid())
    {
        return;
    }

    if (!ActiveFile.IsValid())
    {
        ActiveFile = PersistentManager->CreateActiveFile();
        if (!ActiveFile.IsValid())
        {
            SLS_LOG_WARNING("Failed to create active file, persistence disabled for this session");
        }
    }
}

void FFlusher::CheckRotation()
{
    if (!PersistentManager.IsValid() || !ActiveFile.IsValid())
    {
        return;
    }

    if (PersistentManager->ShouldRotate(ActiveFile))
    {
        SLS_LOG_INFO("File rotation triggered");
        ActiveFile = PersistentManager->RotateFile(ActiveFile);
        if (!ActiveFile.IsValid())
        {
            SLS_LOG_WARNING("File rotation failed, persistence disabled until cleanup");
        }
    }
}

} // namespace sls
} // namespace aliyun
