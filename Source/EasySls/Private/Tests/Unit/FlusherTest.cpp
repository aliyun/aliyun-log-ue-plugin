#if WITH_DEV_AUTOMATION_TESTS

#include "Flusher.h"
#include "Misc/AutomationTest.h"
#include "Common.h"
#include "LogItem.h"
#include "SendTaskQueue.h"

using namespace aliyun::sls;

// 创建测试用配置
static FInnerProducerConfigPtr CreateTestConfig()
{
    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("test.log.aliyuncs.com");
    Config.Project = TEXT("test-project");
    Config.Logstore = TEXT("test-logstore");
    Config.AccessKeyId = TEXT("test-key");
    Config.AccessKeySecret = TEXT("test-secret");
    Config.LogCountPerPackage = 10;
    Config.LogBytesPerPackage = 1024;
    Config.FlushIntervalMs = 100;
    return FInnerProducerConfig::Create(Config);
}

/**
 * Flusher AddLog 测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlusherAddLogTest,
                                 "EasySls.Unit.Flusher.AddLog",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFlusherAddLogTest::RunTest(const FString& Parameters)
{
    auto Config = CreateTestConfig();
    auto TaskQueue = MakeShared<FSendTaskQueue>();
    FFlusher Flusher(Config, TaskQueue);

    // 不启动线程，直接测试 AddLog
    FLogItem Item;
    Item.SetTime(1234567890);
    Item.AddContent(TEXT("level"), TEXT("INFO"));
    Item.AddContent(TEXT("message"), TEXT("Test message"));

    ESlsAddLogResult Result = Flusher.AddLog(Item);
    TestEqual(TEXT("AddLog should return Ok"), Result, ESlsAddLogResult::Ok);

    return true;
}

/**
 * Flusher 内存限制测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlusherMemoryLimitTest,
                                 "EasySls.Unit.Flusher.MemoryLimit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFlusherMemoryLimitTest::RunTest(const FString& Parameters)
{
    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("test.log.aliyuncs.com");
    Config.Project = TEXT("test-project");
    Config.Logstore = TEXT("test-logstore");
    Config.AccessKeyId = TEXT("test-key");        // 添加必填字段
    Config.AccessKeySecret = TEXT("test-secret"); // 添加必填字段
    Config.MaxBufferBytes = 100;                  // 非常小的限制

    auto InnerConfig = FInnerProducerConfig::Create(Config);
    TestNotNull(TEXT("InnerConfig should be valid"), InnerConfig.Get());
    if (!InnerConfig)
    {
        return false;
    }

    auto TaskQueue = MakeShared<FSendTaskQueue>();
    FFlusher Flusher(InnerConfig, TaskQueue);

    // 模拟内存已满
    InnerConfig->MemController->Acquire(100);

    FLogItem Item;
    Item.AddContent(TEXT("test"), TEXT("value"));

    ESlsAddLogResult Result = Flusher.AddLog(Item);
    TestEqual(TEXT("AddLog should return MemoryReachLimit when memory full"),
              Result,
              ESlsAddLogResult::MemoryReachLimit);

    // 释放内存
    InnerConfig->MemController->Release(100);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
