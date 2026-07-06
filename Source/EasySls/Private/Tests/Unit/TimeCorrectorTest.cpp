#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "TimeCorrector.h"

using namespace aliyun::sls;

/**
 * TimeCorrector 禁用状态测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeCorrectorDisabledTest,
                                 "EasySls.Unit.TimeCorrector.Disabled",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTimeCorrectorDisabledTest::RunTest(const FString& Parameters)
{
    FTimeCorrector Corrector(false); // 禁用

    TestFalse(TEXT("Should be disabled"), Corrector.bEnabled);
    TestEqual(TEXT("TimeDelta should be 0"), Corrector.TimeDeltaSeconds.Load(), 0LL);

    // 即使调用 OnServerResponseTime，也不应该更新
    int64 FakeServerTime = FDateTime::UtcNow().ToUnixTimestamp() + 120;
    Corrector.OnServerResponseTime(FakeServerTime);

    TestEqual(TEXT("TimeDelta should still be 0 when disabled"), Corrector.TimeDeltaSeconds.Load(), 0LL);

    // GetRfc1123Date 应该返回当前时间（不校正）
    FString Date = Corrector.GetRfc1123Date();
    TestFalse(TEXT("Date should not be empty"), Date.IsEmpty());

    return true;
}

/**
 * TimeCorrector 时间差小于阈值测试（不修正）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeCorrectorSmallDeltaTest,
                                 "EasySls.Unit.TimeCorrector.SmallDelta",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTimeCorrectorSmallDeltaTest::RunTest(const FString& Parameters)
{
    FTimeCorrector Corrector(true); // 启用

    TestTrue(TEXT("Should be enabled"), Corrector.bEnabled);

    // 设置一个小于 60 秒的时间差
    int64 LocalNow = FDateTime::UtcNow().ToUnixTimestamp();
    int64 ServerTime = LocalNow + 30; // 30 秒差异
    Corrector.OnServerResponseTime(ServerTime);

    // 时间差应该被设置为 0（因为小于阈值）
    TestEqual(TEXT("TimeDelta should be 0 for small delta"), Corrector.TimeDeltaSeconds.Load(), 0LL);

    return true;
}

/**
 * TimeCorrector 时间差大于阈值测试（需要修正）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeCorrectorLargeDeltaTest,
                                 "EasySls.Unit.TimeCorrector.LargeDelta",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTimeCorrectorLargeDeltaTest::RunTest(const FString& Parameters)
{
    FTimeCorrector Corrector(true); // 启用

    // 设置一个大于 60 秒的时间差
    int64 LocalNow = FDateTime::UtcNow().ToUnixTimestamp();
    int64 ServerTime = LocalNow + 120; // 2 分钟差异
    Corrector.OnServerResponseTime(ServerTime);

    // 时间差应该被记录
    int64 Delta = Corrector.TimeDeltaSeconds.Load();
    TestTrue(TEXT("TimeDelta should be around 120"), Delta >= 118 && Delta <= 122);

    return true;
}

/**
 * TimeCorrector 更新间隔过滤测试（5秒内不重复更新）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeCorrectorUpdateIntervalTest,
                                 "EasySls.Unit.TimeCorrector.UpdateInterval",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTimeCorrectorUpdateIntervalTest::RunTest(const FString& Parameters)
{
    FTimeCorrector Corrector(true);

    int64 LocalNow = FDateTime::UtcNow().ToUnixTimestamp();

    // 第一次更新
    int64 ServerTime1 = LocalNow + 120;
    Corrector.OnServerResponseTime(ServerTime1);
    int64 Delta1 = Corrector.TimeDeltaSeconds.Load();

    // 立即再次更新（应该被过滤）
    int64 ServerTime2 = LocalNow + 180;
    Corrector.OnServerResponseTime(ServerTime2);
    int64 Delta2 = Corrector.TimeDeltaSeconds.Load();

    // 时间差应该保持不变（第二次更新被过滤）
    TestEqual(TEXT("Delta should not change within 5s"), Delta1, Delta2);

    return true;
}

/**
 * TimeCorrector 负时间差测试（客户端时间超前）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeCorrectorNegativeDeltaTest,
                                 "EasySls.Unit.TimeCorrector.NegativeDelta",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTimeCorrectorNegativeDeltaTest::RunTest(const FString& Parameters)
{
    FTimeCorrector Corrector(true);

    // 设置一个负的时间差（客户端时间超前 2 分钟）
    int64 LocalNow = FDateTime::UtcNow().ToUnixTimestamp();
    int64 ServerTime = LocalNow - 120; // -2 分钟差异
    Corrector.OnServerResponseTime(ServerTime);

    // 时间差应该被记录为负值
    int64 Delta = Corrector.TimeDeltaSeconds.Load();
    TestTrue(TEXT("TimeDelta should be around -120"), Delta >= -122 && Delta <= -118);

    return true;
}

/**
 * TimeCorrector 从未更新时不修正测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeCorrectorNeverUpdatedTest,
                                 "EasySls.Unit.TimeCorrector.NeverUpdated",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTimeCorrectorNeverUpdatedTest::RunTest(const FString& Parameters)
{
    FTimeCorrector Corrector(true);

    // 从未调用 OnServerResponseTime
    TestEqual(TEXT("TimeDelta should be 0"), Corrector.TimeDeltaSeconds.Load(), 0LL);

    // GetRfc1123Date 应该返回当前时间（不校正）
    FString Date = Corrector.GetRfc1123Date();
    TestFalse(TEXT("Date should not be empty"), Date.IsEmpty());

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
