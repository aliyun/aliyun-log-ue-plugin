#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Common.h"
#include "LogItem.h"
#include "Producer.h"

using namespace aliyun::sls;

static bool GetStressTestConfig(FSlsProducerConfig& OutConfig)
{
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
    OutConfig.Source = TEXT("ue-stress-test");

    return true;
}

// ================================================================
// 稳定性压力测试
//
// 以固定速率（默认 100 条/秒）持续发送日志，验证插件长时间运行的稳定性。
// 通过环境变量控制：
//   SLS_STRESS_DURATION_SEC    - 运行时长（秒），默认 300（5分钟）
//   SLS_STRESS_RATE            - 每秒日志条数，默认 100
//   SLS_STRESS_PERSISTENCE     - 是否启用持久化，默认 0（不启用）
//   SLS_STRESS_LOG_SIZE        - 单条日志 payload 大小（字节），默认 512
//   SLS_STRESS_MAX_FILE_SIZE   - 单个持久化文件最大大小（字节），默认 10485760（10MB）
// ================================================================

static FString GenerateEntropyPayload(int32 TargetBytes)
{
    const int32 GuidLen = 32;
    int32 NumGuids = (TargetBytes + GuidLen - 1) / GuidLen;

    FString Result;
    Result.Reserve(NumGuids * GuidLen);
    for (int32 i = 0; i < NumGuids; ++i)
    {
        Result.Append(FGuid::NewGuid().ToString(EGuidFormats::Digits));
    }
    Result.LeftInline(TargetBytes);
    return Result;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlsStressTest,
                                 "EasySls.Integration.Stress.LongRunning",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlsStressTest::RunTest(const FString& Parameters)
{
    FSlsProducerConfig Config;
    if (!GetStressTestConfig(Config))
    {
        AddWarning(TEXT("Skipping stress test: SLS environment variables not set"));
        return true;
    }

    // 从环境变量读取测试参数
    FString DurationStr = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_STRESS_DURATION_SEC"));
    FString RateStr = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_STRESS_RATE"));
    FString PersistenceStr = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_STRESS_PERSISTENCE"));
    FString LogSizeStr = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_STRESS_LOG_SIZE"));
    FString MaxFileSizeStr = FPlatformMisc::GetEnvironmentVariable(TEXT("SLS_STRESS_MAX_FILE_SIZE"));

    const int32 DurationSec = DurationStr.IsEmpty() ? 300 : FCString::Atoi(*DurationStr);
    const int32 LogsPerSecond = RateStr.IsEmpty() ? 100 : FCString::Atoi(*RateStr);
    const bool bEnablePersistence = !PersistenceStr.IsEmpty() && FCString::Atoi(*PersistenceStr) != 0;
    const int32 LogPayloadSize = LogSizeStr.IsEmpty() ? 512 : FCString::Atoi(*LogSizeStr);
    const int64 MaxFileSize = MaxFileSizeStr.IsEmpty() ? 10 * 1024 * 1024 : FCString::Atoi64(*MaxFileSizeStr);

    Config.FlushIntervalMs = 1000;
    Config.LogCountPerPackage = 1000;
    Config.bEnablePersistence = bEnablePersistence;
    Config.MaxPersistentFileSize = MaxFileSize;
    if (bEnablePersistence)
    {
        Config.PersistenceDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StressTestPersistence"));
    }

    const int64 ExpectedTotalLogs = static_cast<int64>(DurationSec) * LogsPerSecond;

    AddInfo(FString::Printf(TEXT("=== Stress Test Config ===")));
    AddInfo(FString::Printf(TEXT("  Duration:       %d sec"), DurationSec));
    AddInfo(FString::Printf(TEXT("  Rate:           %d logs/sec"), LogsPerSecond));
    AddInfo(FString::Printf(TEXT("  LogPayloadSize: %d bytes"), LogPayloadSize));
    AddInfo(FString::Printf(TEXT("  Expected:       %lld total logs"), ExpectedTotalLogs));
    AddInfo(FString::Printf(TEXT("  Persistence:    %s  Dir: %s"),
                            bEnablePersistence ? TEXT("ON") : TEXT("OFF"),
                            Config.PersistenceDir.IsEmpty() ? TEXT("(none)") : *Config.PersistenceDir));
    AddInfo(FString::Printf(TEXT("  MaxFileSize:    %lld bytes"), MaxFileSize));
    AddInfo(FString::Printf(TEXT("  Endpoint:       %s"), *Config.Endpoint));
    AddInfo(FString::Printf(TEXT("  Project:        %s"), *Config.Project));
    AddInfo(FString::Printf(TEXT("  Logstore:       %s"), *Config.Logstore));

    struct FStressState
    {
        TAtomic<int64> SuccessLogCount{0};
        TAtomic<int64> FailedLogCount{0};
        TAtomic<int32> CallbackCount{0};
        TAtomic<int32> ErrorCount{0};
        TAtomic<int32> LastErrorCode{0};
        FCriticalSection ErrorLock;
        FString LastErrorMessage;
    };
    TSharedPtr<FStressState> State = MakeShared<FStressState>();

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
                State->ErrorCount.IncrementExchange();
                State->LastErrorCode.Store(Result.ResultCode);
                {
                    FScopeLock Lock(&State->ErrorLock);
                    State->LastErrorMessage = Result.ErrorMessage;
                }
            }
        });

    TestTrue(TEXT("Init should succeed"), Producer.Init());
    FPlatformProcess::Sleep(0.2f);

    // -- 发送阶段 --
    AddInfo(TEXT("=== Starting log injection ==="));

    const double IntervalPerLog = 1.0 / LogsPerSecond;
    const double StartTime = FPlatformTime::Seconds();
    const double EndTime = StartTime + DurationSec;

    int64 TotalSent = 0;
    int64 TotalDropped = 0;
    int32 LastReportSec = 0;
    double NextLogTime = StartTime;

    while (FPlatformTime::Seconds() < EndTime)
    {
        double Now = FPlatformTime::Seconds();

        // 计算本轮需要发送多少条以追赶进度
        int32 LogsToSend = 0;
        while (NextLogTime <= Now && NextLogTime < EndTime)
        {
            LogsToSend++;
            NextLogTime += IntervalPerLog;
        }

        for (int32 i = 0; i < LogsToSend; ++i)
        {
            FLogItem Item;
            Item.AddContent(TEXT("level"), TEXT("INFO"));
            Item.AddContent(TEXT("seq"), FString::Printf(TEXT("%lld"), TotalSent));
            Item.AddContent(TEXT("batch_ts"), FString::Printf(TEXT("%.3f"), Now - StartTime));
            Item.AddContent(TEXT("payload"), GenerateEntropyPayload(LogPayloadSize));

            ESlsAddLogResult Result = Producer.AddLog(MoveTemp(Item));
            if (Result == ESlsAddLogResult::Ok)
            {
                TotalSent++;
            }
            else
            {
                TotalDropped++;
            }
        }

        // 每 10 秒打印进度
        int32 ElapsedSec = static_cast<int32>(Now - StartTime);
        if (ElapsedSec >= LastReportSec + 10)
        {
            LastReportSec = ElapsedSec;
            int64 MemUsage = Producer.GetMemoryUsage();

            FString ErrorMsg;
            {
                FScopeLock Lock(&State->ErrorLock);
                ErrorMsg = State->LastErrorMessage;
            }

            AddInfo(FString::Printf(
                TEXT("[%4ds] Sent=%lld Dropped=%lld | Callback=%d Success=%lld Failed=%lld Errors=%d Mem=%lldKB%s"),
                ElapsedSec,
                TotalSent,
                TotalDropped,
                State->CallbackCount.Load(),
                State->SuccessLogCount.Load(),
                State->FailedLogCount.Load(),
                State->ErrorCount.Load(),
                MemUsage / 1024,
                ErrorMsg.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" LastErr=\"%s\""), *ErrorMsg)));
        }

        // 休眠至下一条日志时间，避免空转
        double SleepUntil = FMath::Min(NextLogTime, EndTime);
        double SleepDuration = SleepUntil - FPlatformTime::Seconds();
        if (SleepDuration > 0.0)
        {
            FPlatformProcess::Sleep(static_cast<float>(FMath::Min(SleepDuration, 0.1)));
        }
    }

    AddInfo(FString::Printf(TEXT("=== Injection done: Sent=%lld, Dropped=%lld ==="), TotalSent, TotalDropped));

    // -- 等待回调完成 --
    AddInfo(TEXT("=== Waiting for all callbacks to complete... ==="));

    const double DrainTimeout = 60.0;
    const double DrainStart = FPlatformTime::Seconds();

    while ((State->SuccessLogCount.Load() + State->FailedLogCount.Load()) < TotalSent &&
           (FPlatformTime::Seconds() - DrainStart) < DrainTimeout)
    {
        FPlatformProcess::Sleep(1.0f);

        int32 WaitSec = static_cast<int32>(FPlatformTime::Seconds() - DrainStart);
        if (WaitSec % 5 == 0 && WaitSec > 0)
        {
            AddInfo(FString::Printf(
                TEXT("[drain +%ds] Callback=%d Success=%lld Failed=%lld Pending=%lld"),
                WaitSec,
                State->CallbackCount.Load(),
                State->SuccessLogCount.Load(),
                State->FailedLogCount.Load(),
                TotalSent - State->SuccessLogCount.Load() - State->FailedLogCount.Load()));
        }
    }

    FPlatformProcess::Sleep(2.0f);

    // -- 最终报告 --
    int64 FinalSuccess = State->SuccessLogCount.Load();
    int64 FinalFailed = State->FailedLogCount.Load();
    int64 FinalTotal = FinalSuccess + FinalFailed;
    int64 FinalMem = Producer.GetMemoryUsage();
    double TotalTime = FPlatformTime::Seconds() - StartTime;

    AddInfo(TEXT(""));
    AddInfo(TEXT("========== STRESS TEST REPORT =========="));
    AddInfo(FString::Printf(TEXT("  Duration:          %.1f sec"), TotalTime));
    AddInfo(FString::Printf(TEXT("  Logs Sent:         %lld"), TotalSent));
    AddInfo(FString::Printf(TEXT("  Logs Dropped:      %lld"), TotalDropped));
    AddInfo(FString::Printf(TEXT("  Callback Total:    %lld"), FinalTotal));
    AddInfo(FString::Printf(TEXT("  Callback Success:  %lld"), FinalSuccess));
    AddInfo(FString::Printf(TEXT("  Callback Failed:   %lld"), FinalFailed));
    AddInfo(FString::Printf(TEXT("  Error Count:       %d"), State->ErrorCount.Load()));
    AddInfo(FString::Printf(TEXT("  Final Memory:      %lld bytes"), FinalMem));
    AddInfo(FString::Printf(TEXT("  Avg Rate:          %.1f logs/sec"), TotalSent / TotalTime));
    AddInfo(TEXT("========================================="));

    // -- 验证 --
    TestEqual(TEXT("No logs should be dropped by AddLog"), TotalDropped, 0LL);
    TestEqual(TEXT("All sent logs should be accounted for"), FinalTotal, TotalSent);
    TestEqual(TEXT("All logs should succeed"), FinalSuccess, TotalSent);
    TestEqual(TEXT("No logs should fail"), FinalFailed, 0LL);
    TestTrue(TEXT("Final memory should be near zero (< 1MB)"), FinalMem < 1024 * 1024);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
