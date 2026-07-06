#include "Sender.h"
#include "Async/Async.h"
#include "Flusher.h"
#include "InnerLog.h"
#include "SendTaskQueue.h"
#include "Signature.h"
#include "Utils.h"

namespace aliyun
{
namespace sls
{

// 已知的错误码
static const TCHAR* const LOGE_WRITE_QUOTA_EXCEED = TEXT("WriteQuotaExceed");
static const TCHAR* const LOGE_SHARD_WRITE_QUOTA_EXCEED = TEXT("ShardWriteQuotaExceed");
static const TCHAR* const LOGE_TIME_EXPIRED = TEXT("RequestTimeExpired");

FSender::FSender(TSharedPtr<FSlsHttpClient> InHttpClient,
                 TSharedPtr<FSendTaskQueue> InTaskQueue,
                 FSendCallback InSendCallback)
    : HttpClient(InHttpClient),
      TaskQueue(InTaskQueue),
      AsyncState(MakeShared<FAsyncState>()),
      SendCallback(MoveTemp(InSendCallback))
{
}

FSender::~FSender()
{
    Stop();
}

void FSender::Start()
{
    SLS_LOG_INFO("Sender starting...");
    HttpClient->Start();
    WorkerThread = FRunnableThread::Create(this, TEXT("SlsSender"), 0, TPri_BelowNormal);
    SLS_LOG_INFO("Sender started, WorkerThread=%p", WorkerThread);
}

void FSender::Stop()
{
    SLS_LOG_INFO("Sender stopping...");
    bShutdown.Store(true);
    TaskQueue->Shutdown();

    if (WorkerThread)
    {
        SLS_LOG_DEBUG("Waiting for WorkerThread to complete...");
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }

    while (AsyncState->InFlightRequestCount.Load() > 0)
    {
        SLS_LOG_DEBUG("Waiting for %d in-flight request(s) to complete...", AsyncState->InFlightRequestCount.Load());
        AsyncState->InFlightDoneEvent->Wait(50);
        AsyncState->InFlightDoneEvent->Reset();
    }

    HttpClient->Stop();

    // 处理剩余任务（标记为失败）
    TArray<TSharedPtr<FSendTask>> RemainingTasks;
    TaskQueue->DrainAll(RemainingTasks);
    SLS_LOG_DEBUG("Draining %d remaining tasks", RemainingTasks.Num());
    for (auto& Task : RemainingTasks)
    {
        FinalizeTask(Task, FSlsSendResult(ESlsResult::DropError, TEXT("Sender stopped"), TEXT(""), Task->LogCount()));
    }
    SLS_LOG_INFO("Sender stopped");
}

uint32 FSender::Run()
{
    SLS_LOG_DEBUG("Sender Run() loop started");
    while (!bShutdown.Load())
    {
        TSharedPtr<FSendTask> Task;
        if (TaskQueue->Dequeue(Task, 100))
        {
            SLS_LOG_TRACE("Dequeued task, DataSize=%d, LogCount=%d", Task->Data().Num(), Task->LogCount());
            DoSend(Task);
        }
    }
    SLS_LOG_DEBUG("Sender Run() loop exited");
    return 0;
}

void FSender::DoSend(TSharedPtr<FSendTask> Task)
{
    SLS_LOG_DEBUG("DoSend: DataSize=%d, LogCount=%d, RetryCount=%d",
                  Task->Data().Num(),
                  Task->LogCount(),
                  Task->RetryCount);

    // 检查是否超过最大延迟
    uint32 NowSec = FUtils::GetCurrentTimeSec();
    int32 Age = static_cast<int32>(NowSec - Task->CreateTime);
    if (Age > Task->Config->Config.MaxLogDelayTime)
    {
        SLS_LOG_WARNING("Task too old: Age=%d, MaxDelay=%d", Age, Task->Config->Config.MaxLogDelayTime);
        SendCallback(Task, FSlsSendResult(ESlsResult::SendTimeError, TEXT("Log too old"), TEXT(""), Task->LogCount()));
        return;
    }

    FSlsHttpRequest Request = BuildRequest(Task->Config, Task->Buffer);
    SLS_LOG_DEBUG("Built request: URL=%s, BodySize=%d", *Request.Url, Request.Body.Num());

    // 捕获 TaskQueue 和 SendCallback（TSharedPtr 保证生命周期），不捕获 this
    // 这样即使 Sender 被销毁，回调仍能安全执行
    TSharedPtr<FSendTaskQueue> CapturedTaskQueue = TaskQueue;
    FSendCallback CapturedCallback = SendCallback;
    TSharedPtr<FAsyncState> CapturedAsyncState = AsyncState;

    SLS_LOG_TRACE("Posting async HTTP request...");

    // HTTP 回调在 HTTP 线程，转到低优先级后台线程处理
    if (CapturedAsyncState->InFlightRequestCount.IncrementExchange() == 0)
    {
        CapturedAsyncState->InFlightDoneEvent->Reset();
    }
    HttpClient->PostAsync(Request,
                          [Task, CapturedTaskQueue, CapturedCallback, CapturedAsyncState](
                              const FSlsHttpResponse& Response)
                          {
                              AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
                                        [Task, Response, CapturedTaskQueue, CapturedCallback, CapturedAsyncState]()
                                        {
                                            HandleResponse(
                                                Task, Response, CapturedTaskQueue, CapturedCallback, CapturedAsyncState);
                                        });
                          });
}

void FSender::FinalizeTask(TSharedPtr<FSendTask> Task, const FSlsSendResult& Result) const
{
    if (!Task.IsValid() || !Task->TryMarkCompletionReported())
    {
        return;
    }

    SendCallback(Task, Result);
}

FSlsHttpRequest FSender::BuildRequest(FInnerProducerConfigPtr Config, const FCompressedLogBuffer& Buffer)
{
    FSlsHttpRequest Request;

    // 构建 URL
    const FLogRequestContext& Ctx = Config->LogRequestContext;
    FString Schema = Ctx.bUseHttps ? TEXT("https://") : TEXT("http://");
    Request.Url = Schema + Ctx.Host + Ctx.UriWithQuery;

    // 生成签名和请求头
    FSlsCredentials Credentials = Config->GetCredentials();
    Request.Headers = FSignature::GenerateSignature(Buffer, Credentials, Ctx, *Config->TimeCorrector);

    // 设置请求体
    Request.Body = Buffer.Data;

    // 设置超时
    Request.TimeoutSec = static_cast<float>(Config->Config.SendTimeoutSec);

    return Request;
}

void FSender::HandleResponse(TSharedPtr<FSendTask> Task,
                             const FSlsHttpResponse& Response,
                             TSharedPtr<FSendTaskQueue> TaskQueue,
                             FSendCallback SendCallback,
                             TSharedPtr<FAsyncState> AsyncState)
{
    auto FinishRequest = [&AsyncState]()
    {
        const int32 Remaining = AsyncState->InFlightRequestCount.DecrementExchange() - 1;
        if (Remaining <= 0)
        {
            AsyncState->InFlightDoneEvent->Trigger();
        }
    };

    SLS_LOG_DEBUG("HandleResponse: StatusCode=%d, ErrorCode=%s, RequestId=%s",
                  Response.StatusCode,
                  *Response.ErrorCode,
                  *Response.RequestId);

    // 更新时间校正器（如果服务端返回了时间）
    if (Response.ServerTime > 0 && Task->Config->TimeCorrector.IsValid())
    {
        Task->Config->TimeCorrector->OnServerResponseTime(Response.ServerTime);
    }

    int32 ResultCode = ParseStatusCode(Response.StatusCode, Response.ErrorCode);
    SLS_LOG_TRACE("Parsed ResultCode=%d", ResultCode);

    if (ResultCode == ESlsResult::Ok)
    {
        SLS_LOG_INFO("Send success: RequestId=%s", *Response.RequestId);
        if (Task->TryMarkCompletionReported())
        {
            SendCallback(Task, FSlsSendResult(ESlsResult::Ok, TEXT(""), Response.RequestId, Task->LogCount()));
        }
        FinishRequest();
        return;
    }

    // 判断是否重试
    if (ShouldRetry(ResultCode) && Task->RetryCount < MAX_RETRIES)
    {
        if (!TaskQueue->IsShutdown())
        {
            Task->RetryCount++;
            int32 WaitMs = CalcRetryWaitMs(Task->RetryCount);
            SLS_LOG_WARNING("Will retry: ResultCode=%d, RetryCount=%d, WaitMs=%d", ResultCode, Task->RetryCount, WaitMs);
            TaskQueue->Requeue(Task, WaitMs);
            FinishRequest();
            return;
        }

        SLS_LOG_WARNING("Retry skipped because task queue is shutting down");
    }

    // 最终失败
    SLS_LOG_ERROR("Send failed: ResultCode=%d, ErrorCode=%s, ErrorMessage=%s",
                  ResultCode,
                  *Response.ErrorCode,
                  *Response.ErrorMessage);
    if (Task->TryMarkCompletionReported())
    {
        SendCallback(Task, FSlsSendResult(ResultCode, Response.ErrorMessage, Response.RequestId, Task->LogCount()));
    }
    FinishRequest();
}

int32 FSender::ParseStatusCode(int32 StatusCode, const FString& ErrorCode)
{
    if (StatusCode == 200)
    {
        return ESlsResult::Ok;
    }

    if (StatusCode == 401 || StatusCode == 403)
    {
        return ESlsResult::SendUnauthorized;
    }

    if (StatusCode == 400)
    {
        if (ErrorCode.Contains(LOGE_TIME_EXPIRED))
        {
            return ESlsResult::SendTimeError;
        }
        return ESlsResult::ParametersInvalid;
    }

    if (StatusCode == 500 || StatusCode == 502 || StatusCode == 503)
    {
        if (ErrorCode.Contains(LOGE_WRITE_QUOTA_EXCEED) || ErrorCode.Contains(LOGE_SHARD_WRITE_QUOTA_EXCEED))
        {
            return ESlsResult::SendQuotaError;
        }
        return ESlsResult::SendServerError;
    }

    // 网络错误
    if (StatusCode == 0 || StatusCode == -1)
    {
        return ESlsResult::SendNetworkError;
    }

    return ESlsResult::SendDiscardError;
}

bool FSender::ShouldRetry(int32 ResultCode)
{
    switch (ResultCode)
    {
    case ESlsResult::SendNetworkError:
    case ESlsResult::SendQuotaError:
    case ESlsResult::SendServerError:
    case ESlsResult::SendTimeError:
        return true;
    default:
        return false;
    }
}

int32 FSender::CalcRetryWaitMs(int32 RetryCount)
{
    // 指数退避: 100, 200, 400, 800, ... 最大 30 秒，±10% 随机抖动
    int32 Exp = FMath::Min(RetryCount, 15);
    int32 BaseWait = 100 * (1 << Exp);
    int32 Wait = FMath::Min(BaseWait, 30000);
    int32 Jitter = Wait * FMath::RandRange(-10, 10) / 100;
    return Wait + Jitter;
}

} // namespace sls
} // namespace aliyun
