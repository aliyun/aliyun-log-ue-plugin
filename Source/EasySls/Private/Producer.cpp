#include "Producer.h"
#include "Common.h"
#include "Flusher.h"
#include "HttpClient.h"
#include "InnerLog.h"
#include "PersistentFile.h"
#include "PersistentManager.h"
#include "RecoveryWorker.h"
#include "SendTaskQueue.h"
#include "Sender.h"

namespace aliyun
{
namespace sls
{

FSlsProducer::FSlsProducer(const FSlsProducerConfig& InConfig, FSlsSendCallback InCallback)
    : Config(InConfig),
      UserCallback(MoveTemp(InCallback))
{
}

FSlsProducer::~FSlsProducer()
{
    SLS_LOG_INFO("SlsProducer shutting down...");

    // 1. 停止 Flusher（ActiveFile → SendingFiles_）
    Flusher.Reset();

    // 2. 停止 Sender（所有 in-flight task 的 callback 都已执行，PendingSendCount 归零）
    Sender.Reset();

    // 3. Sender 停止后做最终 CheckSendingFiles（将 remaining sending files 清理或移入 recover/）
    if (PersistentManager.IsValid())
    {
        PersistentManager->CheckSendingFiles();
    }

    // 4. 停止 RecoveryWorker
    RecoveryWorker.Reset();
    TaskQueue.Reset();
    HttpClient.Reset();
    PersistentManager.Reset();

    SLS_LOG_INFO("SlsProducer shutdown complete");
}

bool FSlsProducer::Init()
{
    InnerConfig = FInnerProducerConfig::Create(Config);
    if (!InnerConfig)
    {
        return false;
    }

    // 1. 创建 HttpClient
    HttpClient = MakeShared<FSlsHttpClient>();
    HttpClient->Start();

    // 2. 初始化持久化管理器（创建 temp/ 和 recover/ 目录，移动 crash 遗留文件）
    FPersistenceConfig PersistConfig;
    PersistConfig.bEnablePersistence = Config.bEnablePersistence;
    PersistConfig.PersistenceDir = Config.PersistenceDir;
    PersistConfig.MaxFileSize = Config.MaxPersistentFileSize;
    PersistConfig.MaxFileCount = Config.MaxPersistentFileCount;
    PersistConfig.MaxPersistenceTimeSec = Config.MaxPersistenceTimeSec;

    if (PersistConfig.bEnablePersistence && PersistConfig.PersistenceDir.IsEmpty())
    {
        SLS_LOG_WARNING("PersistenceDir is empty, persistence disabled");
        PersistConfig.bEnablePersistence = false;
    }

    PersistentManager = MakeShared<FPersistentManager>(PersistConfig);

    if (!PersistentManager->Init())
    {
        SLS_LOG_ERROR("Failed to initialize PersistentManager");
        PersistentManager.Reset();
    }

    // 3. 创建 RecoveryWorker（轮询 recover/ 目录处理待恢复文件）
    RecoveryWorker = MakeShared<FRecoveryWorker>(InnerConfig, HttpClient, PersistConfig);
    RecoveryWorker->Start();

    // 4. 创建 TaskQueue
    TaskQueue = MakeShared<FSendTaskQueue>();

    FSlsSendCallback CapturedUserCallback = UserCallback;

    // 5. 创建 Flusher
    Flusher = MakeShared<FFlusher>(InnerConfig, TaskQueue, CapturedUserCallback, PersistentManager);
    Flusher->Start();

    // 6. 创建 Sender（callback 中 Decrement PendingSendCount）
    Sender = MakeShared<FSender>(HttpClient,
                                 TaskQueue,
                                 [CapturedUserCallback](TSharedPtr<FSendTask> Task, const FSlsSendResult& Result)
                                 {
                                     Task->Config->MemController->Release(Task->Data().Num());

                                     if (Task->IsPersisted())
                                     {
                                         if (auto File = Task->FilePtr.Pin())
                                         {
                                             bool bShouldFinalize = Result.IsSuccess() ||
                                                                    !FSender::ShouldRetry(Result.ResultCode);
                                             if (bShouldFinalize)
                                             {
                                                 File->FinalizeBlock(Task->BlockIndex);
                                             }
                                             File->DecrementPendingSend();
                                         }
                                     }

                                     if (CapturedUserCallback)
                                     {
                                         CapturedUserCallback(Result);
                                     }
                                 });
    Sender->Start();

    SLS_LOG_INFO("SlsProducer initialized, Persistence=%s",
                 PersistentManager.IsValid() ? TEXT("Enabled") : TEXT("Disabled"));
    return true;
}

ESlsAddLogResult FSlsProducer::AddLog(const FLogItem& Item, bool bFlush)
{
    return Flusher->AddLog(Item, bFlush);
}

ESlsAddLogResult FSlsProducer::AddLog(FLogItem&& Item, bool bFlush)
{
    return Flusher->AddLog(MoveTemp(Item), bFlush);
}

int64 FSlsProducer::GetMemoryUsage() const
{
    if (InnerConfig && InnerConfig->MemController)
    {
        return InnerConfig->MemController->GetCurrentBytes();
    }
    return -1;
}

} // namespace sls
} // namespace aliyun
