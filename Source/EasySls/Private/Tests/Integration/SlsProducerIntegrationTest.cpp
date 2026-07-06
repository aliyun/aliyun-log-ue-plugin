#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Common.h"
#include "Flusher.h"
#include "LogItem.h"
#include "Producer.h"

using namespace aliyun::sls;

/**
 * @brief 集成测试配置
 *
 * 注意：集成测试需要真实的 SLS 配置
 * 可以通过环境变量或配置文件提供：
 * - SLS_ENDPOINT
 * - SLS_PROJECT
 * - SLS_LOGSTORE
 * - SLS_ACCESS_KEY_ID
 * - SLS_ACCESS_KEY_SECRET
 */
static bool GetIntegrationTestConfig(FSlsProducerConfig& OutConfig)
{
    // 从环境变量获取配置
    FString Endpoint = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_ENDPOINT"));
    FString Project = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_PROJECT"));
    FString Logstore = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_LOGSTORE"));
    FString AccessKeyId = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_ACCESS_KEY_ID"));
    FString AccessKeySecret = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_ACCESS_KEY_SECRET"));

    if (Endpoint.IsEmpty() || Project.IsEmpty() || Logstore.IsEmpty() || AccessKeyId.IsEmpty() ||
        AccessKeySecret.IsEmpty())
    {
        return false;
    }

    OutConfig.Endpoint = Endpoint;
    OutConfig.Project = Project;
    OutConfig.Logstore = Logstore;
    OutConfig.AccessKeyId = AccessKeyId;
    OutConfig.AccessKeySecret = AccessKeySecret;
    OutConfig.Source = TEXT("ue-integration-test");

    return true;
}

/**
 * Producer 端到端测试 - 发送单条日志
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlsProducerE2ESingleLogTest,
                                 "EasySls.Integration.Producer.SingleLog",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlsProducerE2ESingleLogTest::RunTest(const FString& Parameters)
{
    FSlsProducerConfig Config;
    if (!GetIntegrationTestConfig(Config))
    {
        AddWarning(TEXT("Skipping integration test: SLS environment variables not set"));
        return true;
    }

    // 使用 TSharedPtr 管理状态，确保异步回调安全访问
    struct FTestState
    {
        TAtomic<bool> bCallbackCalled{false};
        TAtomic<bool> bSuccess{false};
    };
    TSharedPtr<FTestState> State = MakeShared<FTestState>();

    FSlsProducer Producer(Config,
                          [State](const FSlsSendResult& Result)
                          {
                              State->bCallbackCalled.Store(true);
                              State->bSuccess.Store(Result.IsSuccess());
                              if (!Result.IsSuccess())
                              {
                                  UE_LOG(LogTemp, Error, TEXT("Send failed: %s"), *Result.ErrorMessage);
                              }
                          });

    TestTrue(TEXT("Init should succeed"), Producer.Init());

    // 等待工作线程启动，避免 FlushEvent 丢失
    FPlatformProcess::Sleep(0.1f);

    // 发送日志
    FLogItem Item;
    Item.AddContent(TEXT("level"), TEXT("INFO"));
    Item.AddContent(TEXT("message"), TEXT("Integration test - single log"));
    Item.AddContent(TEXT("timestamp"), FString::FromInt(FDateTime::UtcNow().ToUnixTimestamp()));

    ESlsAddLogResult Result = Producer.AddLog(Item, true); // 立即刷新
    TestEqual(TEXT("AddLog should return Ok"), Result, ESlsAddLogResult::Ok);

    // 等待回调（最多 10 秒）
    double StartTime = FPlatformTime::Seconds();
    while (!State->bCallbackCalled.Load() && (FPlatformTime::Seconds() - StartTime) < 10.0)
    {
        FPlatformProcess::Sleep(0.1f);
    }

    TestTrue(TEXT("Callback should be called"), State->bCallbackCalled.Load());
    TestTrue(TEXT("Send should succeed"), State->bSuccess.Load());

    return true;
}

/**
 * Producer 端到端测试 - 批量发送
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlsProducerE2EBatchTest,
                                 "EasySls.Integration.Producer.Batch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlsProducerE2EBatchTest::RunTest(const FString& Parameters)
{
    FSlsProducerConfig Config;
    if (!GetIntegrationTestConfig(Config))
    {
        AddWarning(TEXT("Skipping integration test: SLS environment variables not set"));
        return true;
    }

    // 设置较短的刷新间隔，加速测试
    Config.FlushIntervalMs = 500;

    // 使用 TSharedPtr 管理状态，确保异步回调安全访问
    struct FTestState
    {
        TAtomic<int32> SuccessCount{0};
        TAtomic<int32> FailCount{0};
    };
    TSharedPtr<FTestState> State = MakeShared<FTestState>();

    FSlsProducer Producer(Config,
                          [State](const FSlsSendResult& Result)
                          {
                              if (Result.IsSuccess())
                              {
                                  State->SuccessCount.IncrementExchange();
                              }
                              else
                              {
                                  State->FailCount.IncrementExchange();
                                  UE_LOG(LogTemp, Error, TEXT("Send failed: %s"), *Result.ErrorMessage);
                              }
                          });

    TestTrue(TEXT("Init should succeed"), Producer.Init());

    // 等待工作线程启动
    FPlatformProcess::Sleep(0.1f);

    // 发送 100 条日志
    const int32 LogCount = 100;
    for (int32 i = 0; i < LogCount; ++i)
    {
        FLogItem Item;
        Item.AddContent(TEXT("level"), TEXT("INFO"));
        Item.AddContent(TEXT("message"), FString::Printf(TEXT("Batch test log %d"), i));
        Item.AddContent(TEXT("index"), FString::FromInt(i));
        Producer.AddLog(Item);
    }

    // 等待超时触发刷新 + 回调（最多 10 秒）
    double StartTime = FPlatformTime::Seconds();
    while ((State->SuccessCount.Load() + State->FailCount.Load()) == 0 && (FPlatformTime::Seconds() - StartTime) < 10.0)
    {
        FPlatformProcess::Sleep(0.1f);
    }

    // 再等待一会儿确保回调完成
    FPlatformProcess::Sleep(1.0f);

    int32 TotalCallbacks = State->SuccessCount.Load() + State->FailCount.Load();
    AddInfo(FString::Printf(TEXT("Success: %d, Failed: %d"), State->SuccessCount.Load(), State->FailCount.Load()));

    TestTrue(TEXT("Should have at least one callback"), TotalCallbacks > 0);
    TestEqual(TEXT("No failures expected"), State->FailCount.Load(), 0);

    return true;
}

/**
 * Producer 端到端测试 - 错误处理（无效凭证）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlsProducerE2EInvalidCredentialsTest,
                                 "EasySls.Integration.Producer.InvalidCredentials",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlsProducerE2EInvalidCredentialsTest::RunTest(const FString& Parameters)
{
    // 告诉框架这个测试预期会有错误日志
    AddExpectedError(TEXT("Send failed"), EAutomationExpectedErrorFlags::Contains, 1);

    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("cn-hangzhou.log.aliyuncs.com");
    Config.Project = TEXT("non-existent-project");
    Config.Logstore = TEXT("non-existent-logstore");
    Config.AccessKeyId = TEXT("invalid-key");
    Config.AccessKeySecret = TEXT("invalid-secret");

    // 使用 TSharedPtr 管理状态，确保异步回调安全访问
    struct FTestState
    {
        TAtomic<bool> bCallbackCalled{false};
        TAtomic<int32> ErrorCode{0};
    };
    TSharedPtr<FTestState> State = MakeShared<FTestState>();

    FSlsProducer Producer(Config,
                          [State](const FSlsSendResult& Result)
                          {
                              State->bCallbackCalled.Store(true);
                              State->ErrorCode.Store(Result.ResultCode);
                          });

    TestTrue(TEXT("Init should succeed"), Producer.Init());

    // 等待工作线程启动，避免 FlushEvent 丢失
    FPlatformProcess::Sleep(0.5f);

    FLogItem Item;
    Item.AddContent(TEXT("test"), TEXT("value"));
    Producer.AddLog(Item, true);

    // 等待回调
    double StartTime = FPlatformTime::Seconds();
    while (!State->bCallbackCalled.Load() && (FPlatformTime::Seconds() - StartTime) < 15.0)
    {
        FPlatformProcess::Sleep(0.1f);
    }

    TestTrue(TEXT("Callback should be called"), State->bCallbackCalled.Load());
    TestNotEqual(TEXT("Should return error code"), State->ErrorCode.Load(), ESlsResult::Ok);

    return true;
}

/**
 * Producer 端到端测试 - 大批量发送（验证日志计数）
 * 发送 10000 条日志，LogCountPerPackage=500，验证所有日志都发送成功
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlsProducerE2ELargeBatchTest,
                                 "EasySls.Integration.Producer.LargeBatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlsProducerE2ELargeBatchTest::RunTest(const FString& Parameters)
{
    FSlsProducerConfig Config;
    if (!GetIntegrationTestConfig(Config))
    {
        AddWarning(TEXT("Skipping integration test: SLS environment variables not set"));
        return true;
    }

    // 配置：每 500 条日志打包一次
    Config.LogCountPerPackage = 500;
    Config.FlushIntervalMs = 1000; // 1 秒刷新间隔

    const int32 TotalLogs = 10000;

    // 使用 TSharedPtr 管理状态
    struct FTestState
    {
        TAtomic<int32> SuccessLogCount{0}; // 成功发送的日志数
        TAtomic<int32> FailedLogCount{0};  // 失败的日志数
        TAtomic<int32> CallbackCount{0};   // 回调次数
    };
    TSharedPtr<FTestState> State = MakeShared<FTestState>();

    FSlsProducer Producer(
        Config,
        [State](const FSlsSendResult& Result)
        {
            State->CallbackCount.IncrementExchange();
            if (Result.IsSuccess())
            {
                State->SuccessLogCount.AddExchange(Result.LogCount);
            }
            else
            {
                State->FailedLogCount.AddExchange(Result.LogCount);
                UE_LOG(LogTemp, Error, TEXT("Send failed: %s, LogCount=%d"), *Result.ErrorMessage, Result.LogCount);
            }
        });

    TestTrue(TEXT("Init should succeed"), Producer.Init());

    // 等待工作线程启动
    FPlatformProcess::Sleep(0.1f);

    // 发送 10000 条日志
    for (int32 i = 0; i < TotalLogs; ++i)
    {
        FLogItem Item;
        Item.AddContent(TEXT("level"), TEXT("INFO"));
        Item.AddContent(TEXT("message"), FString::Printf(TEXT("Large batch test log %d"), i));
        Item.AddContent(TEXT("index"), FString::FromInt(i));
        Producer.AddLog(Item);
    }

    AddInfo(FString::Printf(TEXT("Added %d logs, waiting for callbacks..."), TotalLogs));

    // 等待所有回调完成（最多 60 秒）
    // 预期回调次数：10000 / 500 = 20 次
    double StartTime = FPlatformTime::Seconds();
    while ((State->SuccessLogCount.Load() + State->FailedLogCount.Load()) < TotalLogs &&
           (FPlatformTime::Seconds() - StartTime) < 60.0)
    {
        FPlatformProcess::Sleep(0.5f);

        // 每 5 秒打印进度
        int32 ElapsedSec = static_cast<int32>(FPlatformTime::Seconds() - StartTime);
        if (ElapsedSec % 5 == 0 && ElapsedSec > 0)
        {
            AddInfo(FString::Printf(TEXT("Progress: Success=%d, Failed=%d, Callbacks=%d"),
                                    State->SuccessLogCount.Load(),
                                    State->FailedLogCount.Load(),
                                    State->CallbackCount.Load()));
        }
    }

    // 再等待一会儿确保所有回调完成
    FPlatformProcess::Sleep(2.0f);

    int32 TotalReceived = State->SuccessLogCount.Load() + State->FailedLogCount.Load();
    AddInfo(FString::Printf(TEXT("Final: Success=%d, Failed=%d, Callbacks=%d, Total=%d/%d"),
                            State->SuccessLogCount.Load(),
                            State->FailedLogCount.Load(),
                            State->CallbackCount.Load(),
                            TotalReceived,
                            TotalLogs));

    // 验证
    TestEqual(TEXT("All logs should be accounted for"), TotalReceived, TotalLogs);
    TestEqual(TEXT("All logs should succeed"), State->SuccessLogCount.Load(), TotalLogs);
    TestEqual(TEXT("No logs should fail"), State->FailedLogCount.Load(), 0);

    return true;
}

/**
 * Producer 内存释放测试 - 验证发送完毕后内存控制器归零
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlsProducerMemoryReleaseTest,
                                 "EasySls.Integration.Producer.MemoryRelease",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlsProducerMemoryReleaseTest::RunTest(const FString& Parameters)
{
    FSlsProducerConfig Config;
    if (!GetIntegrationTestConfig(Config))
    {
        AddWarning(TEXT("Skipping integration test: SLS environment variables not set"));
        return true;
    }

    Config.FlushIntervalMs = 500;
    Config.LogCountPerPackage = 100;

    struct FTestState
    {
        TAtomic<int32> SuccessCount{0};
        TAtomic<int32> FailedCount{0};
    };
    TSharedPtr<FTestState> State = MakeShared<FTestState>();

    FSlsProducer Producer(Config,
                          [State](const FSlsSendResult& Result)
                          {
                              if (Result.IsSuccess())
                              {
                                  State->SuccessCount.AddExchange(Result.LogCount);
                              }
                              else
                              {
                                  State->FailedCount.AddExchange(Result.LogCount);
                              }
                          });

    TestTrue(TEXT("Init should succeed"), Producer.Init());

    // 等待工作线程启动
    FPlatformProcess::Sleep(0.1f);

    // 检查初始内存状态
    int64 InitialMemory = Producer.GetMemoryUsage();
    AddInfo(FString::Printf(TEXT("Initial memory: %lld bytes"), InitialMemory));
    TestEqual(TEXT("Initial memory should be 0"), InitialMemory, 0LL);

    // 发送日志
    const int32 TotalLogs = 500;
    for (int32 i = 0; i < TotalLogs; ++i)
    {
        FLogItem Item;
        Item.AddContent(TEXT("test"), FString::Printf(TEXT("memory_test_%d"), i));
        Producer.AddLog(Item);
    }

    // 等待所有回调完成
    double StartTime = FPlatformTime::Seconds();
    while ((State->SuccessCount.Load() + State->FailedCount.Load()) < TotalLogs &&
           (FPlatformTime::Seconds() - StartTime) < 30.0)
    {
        FPlatformProcess::Sleep(0.5f);
    }

    // 额外等待确保内存释放完成
    FPlatformProcess::Sleep(2.0f);

    // 验证内存已释放
    int64 FinalMemory = Producer.GetMemoryUsage();
    AddInfo(FString::Printf(TEXT("Final memory: %lld bytes, Success=%d, Failed=%d"),
                            FinalMemory,
                            State->SuccessCount.Load(),
                            State->FailedCount.Load()));

    TestEqual(TEXT("All logs should be processed"), State->SuccessCount.Load() + State->FailedCount.Load(), TotalLogs);
    TestEqual(TEXT("Memory should be released after all sends complete"), FinalMemory, 0LL);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
