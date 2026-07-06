#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Common.h"
#include "SendTaskQueue.h"

using namespace aliyun::sls;

/**
 * SendTaskQueue 基础功能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSendTaskQueueBasicTest,
                                 "EasySls.Unit.SendTaskQueue.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSendTaskQueueBasicTest::RunTest(const FString& Parameters)
{
    FSendTaskQueue Queue(100);

    // 测试初始状态
    TestEqual(TEXT("Initial size should be 0"), Queue.QueueSize.Load(), 0);
    TestTrue(TEXT("Should not be full initially"), Queue.QueueSize.Load() < Queue.MaxQueueSize);
    TestFalse(TEXT("Should not be shutdown initially"), Queue.bShutdown.Load());

    return true;
}

/**
 * SendTaskQueue Submit 测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSendTaskQueueSubmitTest,
                                 "EasySls.Unit.SendTaskQueue.Submit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSendTaskQueueSubmitTest::RunTest(const FString& Parameters)
{
    FSendTaskQueue Queue(10);

    // 创建测试任务
    TSharedPtr<FSendTask> Task = MakeShared<FSendTask>();
    Task->Buffer.RawSize = 100;

    // 测试提交
    bool bResult = Queue.Submit(Task);
    TestTrue(TEXT("Submit should succeed"), bResult);
    TestEqual(TEXT("Size should be 1 after submit"), Queue.QueueSize.Load(), 1);

    // 测试取出
    TSharedPtr<FSendTask> OutTask;
    bResult = Queue.Dequeue(OutTask, 0);
    TestTrue(TEXT("Dequeue should succeed"), bResult);
    TestEqual(TEXT("Task should match"), OutTask->RawSize(), 100LL);
    TestEqual(TEXT("Size should be 0 after dequeue"), Queue.QueueSize.Load(), 0);

    return true;
}

/**
 * SendTaskQueue 容量限制测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSendTaskQueueCapacityTest,
                                 "EasySls.Unit.SendTaskQueue.Capacity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSendTaskQueueCapacityTest::RunTest(const FString& Parameters)
{
    FSendTaskQueue Queue(3);

    // 填满队列
    for (int32 i = 0; i < 3; ++i)
    {
        TSharedPtr<FSendTask> Task = MakeShared<FSendTask>();
        bool bResult = Queue.Submit(Task);
        TestTrue(FString::Printf(TEXT("Submit %d should succeed"), i), bResult);
    }

    TestTrue(TEXT("Queue should be full"), Queue.QueueSize.Load() >= Queue.MaxQueueSize);

    // 超出容量应该失败
    TSharedPtr<FSendTask> ExtraTask = MakeShared<FSendTask>();
    bool bResult = Queue.Submit(ExtraTask);
    TestFalse(TEXT("Submit beyond capacity should fail"), bResult);

    return true;
}

/**
 * SendTaskQueue Requeue（重试）测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSendTaskQueueRequeueTest,
                                 "EasySls.Unit.SendTaskQueue.Requeue",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSendTaskQueueRequeueTest::RunTest(const FString& Parameters)
{
    FSendTaskQueue Queue(100);

    // 创建任务并放入重试队列（延迟 0ms）
    TSharedPtr<FSendTask> Task = MakeShared<FSendTask>();
    Task->RetryCount = 1;
    Queue.Requeue(Task, 0);

    // 立即取出（延迟 0ms 应该立即就绪）
    FPlatformProcess::Sleep(0.01f); // 给一点时间
    TSharedPtr<FSendTask> OutTask;
    bool bResult = Queue.Dequeue(OutTask, 100);

    TestTrue(TEXT("Dequeue requeued task should succeed"), bResult);
    if (bResult)
    {
        TestEqual(TEXT("RetryCount should match"), OutTask->RetryCount, 1);
    }

    return true;
}

/**
 * SendTaskQueue Shutdown 测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSendTaskQueueShutdownTest,
                                 "EasySls.Unit.SendTaskQueue.Shutdown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSendTaskQueueShutdownTest::RunTest(const FString& Parameters)
{
    FSendTaskQueue Queue(100);

    // 提交任务
    TSharedPtr<FSendTask> Task = MakeShared<FSendTask>();
    Queue.Submit(Task);

    // 关闭队列
    Queue.Shutdown();
    TestTrue(TEXT("Should be shutdown"), Queue.bShutdown.Load());

    // 关闭后不能提交
    TSharedPtr<FSendTask> Task2 = MakeShared<FSendTask>();
    bool bResult = Queue.Submit(Task2);
    TestFalse(TEXT("Submit after shutdown should fail"), bResult);

    // 取出所有剩余任务
    TArray<TSharedPtr<FSendTask>> Remaining;
    Queue.DrainAll(Remaining);
    TestEqual(TEXT("Should have 1 remaining task"), Remaining.Num(), 1);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
