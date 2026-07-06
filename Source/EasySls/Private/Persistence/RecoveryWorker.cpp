#include "RecoveryWorker.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Flusher.h"
#include "HttpClient.h"
#include "InnerLog.h"
#include "Sender.h"

namespace aliyun
{
namespace sls
{

FRecoveryWorker::FRecoveryWorker(FInnerProducerConfigPtr InConfig,
                                 TSharedPtr<FSlsHttpClient> InHttpClient,
                                 const FPersistenceConfig& InPersistConfig)
    : Config_(InConfig),
      HttpClient_(InHttpClient),
      PersistConfig_(InPersistConfig),
      RecoverDir_(FPaths::Combine(InPersistConfig.PersistenceDir, TEXT("recover")))
{
}

FRecoveryWorker::~FRecoveryWorker()
{
    Stop();
}

void FRecoveryWorker::Start()
{
    SLS_LOG_INFO("RecoveryWorker starting (polling recover/ dir: %s)", *RecoverDir_);
    WorkerThread_ = FRunnableThread::Create(this, TEXT("SlsRecoveryWorker"), 0, TPri_BelowNormal);
}

void FRecoveryWorker::Stop()
{
    SLS_LOG_INFO("RecoveryWorker stopping...");
    bShutdown_.Store(true);
    ShutdownEvent_->Trigger();

    if (WorkerThread_)
    {
        WorkerThread_->WaitForCompletion();
        delete WorkerThread_;
        WorkerThread_ = nullptr;
    }

    SLS_LOG_INFO("RecoveryWorker stopped");
}

uint32 FRecoveryWorker::Run()
{
    SLS_LOG_INFO("RecoveryWorker Run() started");

    while (!bShutdown_.Load())
    {
        TSharedPtr<FPersistentFile> File = LoadEarliestRecoverFile();

        if (!File.IsValid())
        {
            bIdle_.Store(true);
            ShutdownEvent_->Wait(PollIntervalMs_);
            continue;
        }

        bIdle_.Store(false);
        ProcessFile(File);
    }

    bIdle_.Store(true);
    SLS_LOG_INFO("RecoveryWorker Run() exited");
    return 0;
}

int64 FRecoveryWorker::ParseCreateTimeFromBaseName(const FString& BaseName)
{
    // sls_log_{timestamp}_{guid}
    TArray<FString> Parts;
    BaseName.ParseIntoArray(Parts, TEXT("_"));
    if (Parts.Num() >= 3)
    {
        return FCString::Atoi64(*Parts[2]);
    }
    return 0;
}

TSharedPtr<FPersistentFile> FRecoveryWorker::LoadEarliestRecoverFile()
{
    if (RecoverDir_.IsEmpty())
    {
        return nullptr;
    }

    TArray<FString> FoundFiles;
    FString SearchPath = FPaths::Combine(RecoverDir_, TEXT("sls_log_*.dat"));
    IFileManager::Get().FindFiles(FoundFiles, *SearchPath, true, false);

    TArray<FString> BaseNames;
    for (const FString& FileName : FoundFiles)
    {
        BaseNames.Add(FPaths::GetBaseFilename(FileName));
    }

    // 按字符串排序（文件名中的时间戳部分天然有序， 当时间戳位数不同时也可以容忍）
    BaseNames.Sort();

    int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    for (const FString& BaseName : BaseNames)
    {
        // 清理过期文件
        int64 CreateTime = ParseCreateTimeFromBaseName(BaseName);
        if (CreateTime > 0 && (Now - CreateTime) > PersistConfig_.MaxPersistenceTimeSec)
        {
            SLS_LOG_INFO("Recovery: deleting expired file %s (age=%llds)", *BaseName, Now - CreateTime);
            PlatformFile.DeleteFile(*FPaths::Combine(RecoverDir_, BaseName + TEXT(".dat")));
            PlatformFile.DeleteFile(*FPaths::Combine(RecoverDir_, BaseName + TEXT(".meta")));
            continue;
        }

        auto File = MakeShared<FPersistentFile>(BaseName, RecoverDir_);
        if (!File->LoadAndValidate())
        {
            SLS_LOG_WARNING("Recovery: validation failed for %s, deleting", *BaseName);
            File->DeleteFiles();
            continue;
        }

        return File;
    }

    return nullptr;
}

void FRecoveryWorker::ProcessFile(TSharedPtr<FPersistentFile> File)
{
    int32 BlockCount = File->GetTotalBlockCount();
    SLS_LOG_INFO("Recovery: processing file %s (blocks=%d)", *File->GetBaseName(), BlockCount);

    for (int32 i = 0; i < BlockCount && !bShutdown_.Load(); ++i)
    {
        if (File->IsFinalized(i))
        {
            SLS_LOG_DEBUG("Recovery: Block %d already finalized, skipping", i);
            continue;
        }

        TArray<uint8> Payload;
        if (!File->ReadBlockPayload(i, Payload))
        {
            SLS_LOG_ERROR("Recovery: Block %d read/CRC failed, deleting file %s", i, *File->GetBaseName());
            File->DeleteFiles();
            return;
        }

        const FBlockHeader& Header = File->GetBlockHeader(i);
        FSlsSendResult Result = SyncSendWithRetry(Payload, Header);

        if (bShutdown_.Load())
        {
            break;
        }

        if (Result.IsSuccess())
        {
            SLS_LOG_INFO("Recovery: Block %d sent successfully", i);
        }
        else
        {
            SLS_LOG_WARNING("Recovery: Block %d send failed (non-retryable): %s", i, *Result.ErrorMessage);
        }

        File->FinalizeBlock(i);
    }

    if (!bShutdown_.Load())
    {
        File->DeleteFiles();
        SLS_LOG_INFO("Recovery: completed file %s", *File->GetBaseName());
    }
}

FSlsSendResult FRecoveryWorker::SyncSendWithRetry(const TArray<uint8>& BlockData, const FBlockHeader& Header)
{
    int32 RetryCount = 0;

    while (true)
    {
        if (bShutdown_.Load())
        {
            return FSlsSendResult(ESlsResult::DropError, TEXT("Shutdown"), TEXT(""), 0);
        }

        FSlsSendResult Result = SyncSendHttp(BlockData, Header);

        if (Result.IsSuccess())
        {
            return Result;
        }

        if (!FSender::ShouldRetry(Result.ResultCode))
        {
            return Result;
        }

        RetryCount++;
        int32 WaitMs = FSender::CalcRetryWaitMs(RetryCount);

        SLS_LOG_DEBUG("Recovery: Retry %d, wait %dms", RetryCount, WaitMs);

        if (ShutdownEvent_->Wait(WaitMs))
        {
            return FSlsSendResult(ESlsResult::DropError, TEXT("Shutdown"), TEXT(""), 0);
        }
    }
}

struct FSyncHttpState
{
    FEventRef CompletionEvent{EEventMode::ManualReset};
    FSlsHttpResponse Response;
    TAtomic<bool> bCompleted{false};
};

FSlsSendResult FRecoveryWorker::SyncSendHttp(const TArray<uint8>& BlockData, const FBlockHeader& Header)
{
    if (bShutdown_.Load())
    {
        return FSlsSendResult(ESlsResult::DropError, TEXT("Shutdown"), TEXT(""), 0);
    }

    auto State = MakeShared<FSyncHttpState>();

    FCompressedLogBuffer Buffer;
    Buffer.Data = BlockData;
    Buffer.RawSize = Header.RawSize;
    Buffer.CompressType = (Header.Flags & EBlockFlags::CompressLZ4) ? SlsHeaders::XLogCompressTypeLz4 : FString();
    Buffer.LogCount = Header.LogCount;

    FSlsHttpRequest Request = FSender::BuildRequest(Config_, Buffer);

    HttpClient_->PostAsync(Request,
                           [State](const FSlsHttpResponse& InResponse)
                           {
                               State->Response = InResponse;
                               State->bCompleted.Store(true);
                               State->CompletionEvent->Trigger();
                           });

    constexpr int32 PollInterval = 100;
    constexpr int32 MaxWait = 60000;
    int32 WaitedMs = 0;

    while (!State->bCompleted.Load() && WaitedMs < MaxWait)
    {
        if (bShutdown_.Load())
        {
            SLS_LOG_INFO("RecoveryWorker: HTTP request interrupted by shutdown");
            return FSlsSendResult(ESlsResult::DropError, TEXT("Shutdown"), TEXT(""), 0);
        }

        State->CompletionEvent->Wait(PollInterval);
        WaitedMs += PollInterval;
    }

    if (!State->bCompleted.Load())
    {
        return FSlsSendResult(ESlsResult::SendNetworkError, TEXT("Timeout"), TEXT(""), 0);
    }

    if (State->Response.ServerTime > 0 && Config_->TimeCorrector.IsValid())
    {
        Config_->TimeCorrector->OnServerResponseTime(State->Response.ServerTime);
    }

    int32 ResultCode = FSender::ParseStatusCode(State->Response.StatusCode, State->Response.ErrorCode);
    return FSlsSendResult(ResultCode, State->Response.ErrorMessage, State->Response.RequestId, 0);
}

} // namespace sls
} // namespace aliyun
