#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Common.h"
#include "HttpClient.h"
#include "SendTaskQueue.h"
#include "Sender.h"

using namespace aliyun::sls;

// 创建测试用配置
static FInnerProducerConfigPtr CreateSenderTestConfig()
{
    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("test.log.aliyuncs.com");
    Config.Project = TEXT("test-project");
    Config.Logstore = TEXT("test-logstore");
    Config.AccessKeyId = TEXT("test-key");
    Config.AccessKeySecret = TEXT("test-secret");
    Config.SendTimeoutSec = 5;
    return FInnerProducerConfig::Create(Config);
}

class FDelayedMockHttpClient : public FSlsHttpClient
{
public:
    virtual void PostAsync(const FSlsHttpRequest& Request, FSlsHttpCallback Callback) override
    {
        RequestCount.IncrementExchange();
        {
            FScopeLock ScopeLock(&Lock);
            PendingCallback = MoveTemp(Callback);
        }
        RequestQueuedEvent->Trigger();
    }

    void Complete(const FSlsHttpResponse& Response)
    {
        FSlsHttpCallback CallbackCopy;
        {
            FScopeLock ScopeLock(&Lock);
            CallbackCopy = PendingCallback;
        }

        if (CallbackCopy)
        {
            CallbackCopy(Response);
        }
    }

    TAtomic<int32> RequestCount{0};
    FEventRef RequestQueuedEvent{EEventMode::ManualReset};
    FCriticalSection Lock;
    FSlsHttpCallback PendingCallback;
};

/**
 * Sender 基础测试 - 测试 Sender 创建和生命周期
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenderBasicTest,
                                 "EasySls.Unit.Sender.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSenderBasicTest::RunTest(const FString& Parameters)
{
    auto Config = CreateSenderTestConfig();
    auto TaskQueue = MakeShared<FSendTaskQueue>();
    auto HttpClient = MakeShared<FSlsHttpClient>();

    bool bCallbackCalled = false;

    FSender Sender(HttpClient,
                   TaskQueue,
                   [&](TSharedPtr<FSendTask> Task, const FSlsSendResult& Result)
                   {
                       bCallbackCalled = true;
                       if (Task && Task->Config && Task->Config->MemController)
                       {
                           Task->Config->MemController->Release(Task->Data().Num());
                       }
                   });

    // 测试启动和停止
    HttpClient->Start();
    Sender.Start();

    // 短暂等待
    FPlatformProcess::Sleep(0.1f);

    // 停止
    Sender.Stop();
    HttpClient->Stop();

    // 基础测试通过（没有崩溃）
    TestTrue(TEXT("Sender lifecycle test passed"), true);

    return true;
}

/**
 * SendTaskQueue 和 Sender 集成测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenderTaskQueueIntegrationTest,
                                 "EasySls.Unit.Sender.TaskQueueIntegration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSenderTaskQueueIntegrationTest::RunTest(const FString& Parameters)
{
    auto Config = CreateSenderTestConfig();
    auto TaskQueue = MakeShared<FSendTaskQueue>(10);

    // 测试任务提交到队列
    TSharedPtr<FSendTask> Task = MakeShared<FSendTask>();
    Task->Config = Config;
    Task->Buffer.Data.SetNum(100);
    Task->Buffer.RawSize = 100;
    Task->CreateTime = FDateTime::UtcNow().ToUnixTimestamp();

    bool bSubmitResult = TaskQueue->Submit(Task);
    TestTrue(TEXT("Task should be submitted successfully"), bSubmitResult);
    TestEqual(TEXT("Queue should have 1 task"), TaskQueue->QueueSize.Load(), 1);

    // 测试取出任务
    TSharedPtr<FSendTask> OutTask;
    bool bDequeueResult = TaskQueue->Dequeue(OutTask, 0);
    TestTrue(TEXT("Task should be dequeued successfully"), bDequeueResult);
    TestEqual(TEXT("Queue should be empty"), TaskQueue->QueueSize.Load(), 0);

    if (bDequeueResult && OutTask)
    {
        TestEqual(TEXT("Task data size should match"), OutTask->Data().Num(), 100);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenderStopWaitsInflightTest,
                                 "EasySls.Unit.Sender.StopWaitsInflight",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSenderStopWaitsInflightTest::RunTest(const FString& Parameters)
{
    auto Config = CreateSenderTestConfig();
    auto TaskQueue = MakeShared<FSendTaskQueue>(10);
    auto HttpClient = MakeShared<FDelayedMockHttpClient>();

    TAtomic<int32> CallbackCount{0};
    TAtomic<int64> FinalMemory{0};

    FSender Sender(HttpClient,
                   TaskQueue,
                   [&](TSharedPtr<FSendTask> Task, const FSlsSendResult& Result)
                   {
                       CallbackCount.IncrementExchange();
                       if (Task && Task->Config && Task->Config->MemController)
                       {
                           Task->Config->MemController->Release(Task->Data().Num());
                           FinalMemory.Store(Task->Config->MemController->GetCurrentBytes());
                       }
                   });

    HttpClient->Start();
    Sender.Start();

    TSharedPtr<FSendTask> Task = MakeShared<FSendTask>();
    Task->Config = Config;
    Task->Buffer.Data.SetNum(128);
    Task->Buffer.RawSize = 128;
    Task->Buffer.LogCount = 1;
    Task->CreateTime = FDateTime::UtcNow().ToUnixTimestamp();
    Config->MemController->Acquire(Task->Data().Num());

    TestTrue(TEXT("Submit should succeed"), TaskQueue->Submit(Task));
    TestTrue(TEXT("Request should be queued"), HttpClient->RequestQueuedEvent->Wait(1000));

    TFuture<void> StopFuture = Async(EAsyncExecution::Thread,
                                     [&Sender]()
                                     {
                                         Sender.Stop();
                                     });

    FPlatformProcess::Sleep(0.05f);
    TestEqual(TEXT("Callback should not run before HTTP completion"), CallbackCount.Load(), 0);

    HttpClient->Complete(FSlsHttpResponse::Success(TEXT("delayed-request")));
    StopFuture.Wait();

    HttpClient->Complete(FSlsHttpResponse::Success(TEXT("duplicate-request")));

    TestEqual(TEXT("Callback should run exactly once"), CallbackCount.Load(), 1);
    TestEqual(TEXT("Memory should be fully released"), FinalMemory.Load(), 0LL);

    HttpClient->Stop();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
