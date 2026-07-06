#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Common.h"
#include "HttpClient.h"
#include "LogGroupBuilder.h"
#include "LogItem.h"

class FPersistenceAndRecoveryTest;
class FRotationRecoveryTest;

#ifdef UNIT_TEST_FRIEND
#undef UNIT_TEST_FRIEND
#endif
#define UNIT_TEST_FRIEND                        \
    friend class ::FPersistenceAndRecoveryTest; \
    friend class ::FRotationRecoveryTest

#include "PersistentFile.h"
#include "PersistentManager.h"
#include "RecoveryWorker.h"
#include "ThirdParty.h"

using namespace aliyun::sls;

// ================================================================
// Mock HTTP Client
// ================================================================

class FMockHttpClient : public FSlsHttpClient
{
public:
    FSlsHttpResponse MockResponse = FSlsHttpResponse::Success(TEXT("mock-request-id"));
    TAtomic<int32> RequestCount{0};
    FCriticalSection Lock;
    TArray<TArray<uint8>> CapturedBodies;

    virtual void PostAsync(const FSlsHttpRequest& Request, FSlsHttpCallback Callback) override
    {
        RequestCount.IncrementExchange();
        {
            FScopeLock ScopeLock(&Lock);
            CapturedBodies.Add(Request.Body);
        }
        if (Callback)
        {
            Callback(MockResponse);
        }
    }

    virtual void Start() override {}
    virtual void Stop() override {}
};

// ================================================================
// Helpers
// ================================================================

static FString CreateTestPersistenceDir()
{
    FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(),
                                  TEXT("TestSlsPersistence"),
                                  FGuid::NewGuid().ToString(EGuidFormats::Short));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*Dir);
    return Dir;
}

static void CleanupTestDir(const FString& Dir)
{
    IFileManager::Get().DeleteDirectory(*Dir, false, true);
}

static TArray<FString> ListDatFileBaseNames(const FString& Dir)
{
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("sls_log_*.dat")), true, false);

    TArray<FString> BaseNames;
    for (const FString& FileName : Files)
    {
        BaseNames.Add(FPaths::GetBaseFilename(FileName));
    }
    return BaseNames;
}

static bool BuildCompressedBlock(const FString& BlockId, int32 LogCount, TArray<uint8>& OutCompressed, int64& OutRawSize)
{
    FLogGroupBuilder Builder;
    Builder.SetSource(TEXT("integration-test"));

    for (int32 i = 0; i < LogCount; ++i)
    {
        FLogItem Item;
        Item.AddContent(TEXT("blockId"), BlockId);
        Item.AddContent(TEXT("index"), FString::FromInt(i));
        Item.AddContent(TEXT("message"), FString::Printf(TEXT("persistence_test_%d"), i));
        Builder.AddLog(MoveTemp(Item));
    }

    TArray<uint8> Serialized = Builder.Serialize();
    if (Serialized.Num() == 0)
    {
        return false;
    }

    OutRawSize = Serialized.Num();
    return thirdparty::Lz4Compress(Serialized, OutCompressed) && OutCompressed.Num() > 0;
}

static FInnerProducerConfigPtr CreateMockConfig()
{
    FSlsProducerConfig Cfg;
    Cfg.Endpoint = TEXT("mock.log.aliyuncs.com");
    Cfg.Project = TEXT("mock-project");
    Cfg.Logstore = TEXT("mock-logstore");
    Cfg.AccessKeyId = TEXT("mock-key");
    Cfg.AccessKeySecret = TEXT("mock-secret");
    return FInnerProducerConfig::Create(Cfg);
}

static bool WaitForIdle(TAtomic<bool>& bIdle, double TimeoutSec = 10.0)
{
    double Start = FPlatformTime::Seconds();
    while (!bIdle.Load() && (FPlatformTime::Seconds() - Start) < TimeoutSec)
    {
        FPlatformProcess::Sleep(0.05f);
    }
    return bIdle.Load();
}

// ================================================================
// Test 1: 基本持久化与恢复
//
// Phase 1: 写入未 finalized 的 block（模拟网络故障）
// Phase 2: 验证 block 数据完整性
// Phase 3: 将文件放入 recover/ 目录，RecoveryWorker 轮询处理
// ================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistenceAndRecoveryTest,
                                 "EasySls.Integration.Persistence.PersistenceAndRecovery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPersistenceAndRecoveryTest::RunTest(const FString& Parameters)
{
    FString TestDir = CreateTestPersistenceDir();
    FString RecoverDir = FPaths::Combine(TestDir, TEXT("recover"));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*RecoverDir);

    FString BlockId = FGuid::NewGuid().ToString(EGuidFormats::Short);

    AddInfo(FString::Printf(TEXT("TestDir: %s"), *TestDir));

    // ================================================================
    // Phase 1: 创建持久化文件，写入未 finalized 的 block
    // ================================================================
    AddInfo(TEXT("=== Phase 1: Write persisted blocks ==="));

    const int32 BlockCount = 3;
    const int32 LogsPerBlock = 5;
    FString BaseName = FPersistentFile::GenerateBaseName();

    TArray<TArray<uint8>> OriginalPayloads;

    {
        auto File = MakeShared<FPersistentFile>(BaseName, RecoverDir);
        TestTrue(TEXT("Phase1: InitNew"), File->InitNew());
        TestTrue(TEXT("Phase1: IsAppending"), File->bIsAppending_);

        for (int32 b = 0; b < BlockCount; ++b)
        {
            TArray<uint8> Compressed;
            int64 RawSize = 0;
            FString CurrentBlockId = FString::Printf(TEXT("%s_block%d"), *BlockId, b);

            TestTrue(FString::Printf(TEXT("Phase1: Build block %d"), b),
                     BuildCompressedBlock(CurrentBlockId, LogsPerBlock, Compressed, RawSize));

            OriginalPayloads.Add(Compressed);

            int32 BlockIndex = File->AppendBlock(Compressed, RawSize, LogsPerBlock, true);
            TestTrue(FString::Printf(TEXT("Phase1: AppendBlock %d"), b), BlockIndex >= 0);
        }

        File->FinishAppending();
        TestFalse(TEXT("Phase1: Not Appending after FinishAppending"), File->bIsAppending_);
    }

    TestEqual(TEXT("Phase1: Should have 1 .dat file in recover/"), ListDatFileBaseNames(RecoverDir).Num(), 1);

    // ================================================================
    // Phase 2: 验证 block 数据完整性
    // ================================================================
    AddInfo(TEXT("=== Phase 2: Verify block data integrity ==="));

    {
        auto File = MakeShared<FPersistentFile>(BaseName, RecoverDir);
        TestTrue(TEXT("Phase2: LoadAndValidate"), File->LoadAndValidate());
        TestFalse(TEXT("Phase2: Not Appending after load"), File->bIsAppending_);
        TestEqual(TEXT("Phase2: BlockCount"), File->GetTotalBlockCount(), BlockCount);

        int32 UnfinalizedCount = 0;

        for (int32 i = 0; i < File->GetTotalBlockCount(); ++i)
        {
            const FBlockHeader& Header = File->GetBlockHeader(i);
            TestEqual(FString::Printf(TEXT("Phase2: Block %d magic"), i), Header.Magic, BLOCK_MAGIC);
            TestTrue(FString::Printf(TEXT("Phase2: Block %d RawSize > 0"), i), Header.RawSize > 0);
            TestEqual(FString::Printf(TEXT("Phase2: Block %d LogCount"), i), (int32)Header.LogCount, LogsPerBlock);

            TArray<uint8> Payload;
            TestTrue(FString::Printf(TEXT("Phase2: ReadBlockPayload %d"), i), File->ReadBlockPayload(i, Payload));
            TestEqual(FString::Printf(TEXT("Phase2: Block %d payload matches"), i), Payload, OriginalPayloads[i]);

            if (!File->IsFinalized(i))
            {
                UnfinalizedCount++;
            }
        }

        TestEqual(TEXT("Phase2: All blocks unfinalized"), UnfinalizedCount, BlockCount);
    }

    // ================================================================
    // Phase 3: RecoveryWorker 轮询 recover/ 目录
    // ================================================================
    AddInfo(TEXT("=== Phase 3: RecoveryWorker polls recover/ ==="));

    auto MockHttp = MakeShared<FMockHttpClient>();

    {
        auto InnerConfig = CreateMockConfig();
        FPersistenceConfig WorkerPersistConfig;
        WorkerPersistConfig.PersistenceDir = TestDir;
        FRecoveryWorker Worker(InnerConfig, MockHttp, WorkerPersistConfig);
        Worker.Start();

        TestTrue(TEXT("Phase3: Worker should become idle"), WaitForIdle(Worker.bIdle_));
    }

    int32 ReqCount = MockHttp->RequestCount.Load();
    AddInfo(FString::Printf(TEXT("Mock received %d request(s)"), ReqCount));
    TestEqual(TEXT("Phase3: request count == BlockCount"), ReqCount, BlockCount);

    {
        FScopeLock ScopeLock(&MockHttp->Lock);
        for (int32 i = 0; i < FMath::Min(ReqCount, BlockCount); ++i)
        {
            TestEqual(FString::Printf(TEXT("Phase3: Body %d matches"), i),
                      MockHttp->CapturedBodies[i],
                      OriginalPayloads[i]);
        }
    }

    TestEqual(TEXT("Phase3: .dat files cleaned from recover/"), ListDatFileBaseNames(RecoverDir).Num(), 0);

    CleanupTestDir(TestDir);
    AddInfo(TEXT("=== Test 1 completed ==="));
    return true;
}

// ================================================================
// Test 2: PersistentManager 轮转 → CheckSendingFiles → RecoveryWorker
//
// 验证端到端流程：
// 1. PersistentManager 创建文件，写入 block
// 2. RotateFile → SendingFiles_
// 3. CheckSendingFiles → PendingSendCount==0, !AllFinalized → move to recover/
// 4. RecoveryWorker 轮询 recover/ 处理文件
// ================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRotationRecoveryTest,
                                 "EasySls.Integration.Persistence.RotationRecovery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRotationRecoveryTest::RunTest(const FString& Parameters)
{
    FString TestDir = CreateTestPersistenceDir();
    AddInfo(FString::Printf(TEXT("TestDir: %s"), *TestDir));

    auto InnerConfig = CreateMockConfig();
    auto MockHttp = MakeShared<FMockHttpClient>();

    // 1. 创建 PersistentManager
    FPersistenceConfig PersistConfig;
    PersistConfig.bEnablePersistence = true;
    PersistConfig.PersistenceDir = TestDir;
    PersistConfig.MaxFileSize = 100;
    PersistConfig.MaxFileCount = 10;

    FPersistentManager Manager(PersistConfig);
    TestTrue(TEXT("Manager Init"), Manager.Init());

    // 2. 创建活跃文件，写入 block（不 finalize，模拟发送失败）
    const int32 BlocksPerFile = 2;
    const int32 LogsPerBlock = 3;
    TArray<TArray<uint8>> AllPayloads;

    TSharedPtr<FPersistentFile> File1 = Manager.CreateActiveFile();
    TestNotNull(TEXT("File1 created"), File1.Get());

    for (int32 b = 0; b < BlocksPerFile; ++b)
    {
        TArray<uint8> Compressed;
        int64 RawSize = 0;
        FString BId = FString::Printf(TEXT("rot_block_%d"), b);
        TestTrue(FString::Printf(TEXT("Build block %d"), b),
                 BuildCompressedBlock(BId, LogsPerBlock, Compressed, RawSize));
        AllPayloads.Add(Compressed);
        File1->AppendBlock(Compressed, RawSize, LogsPerBlock, true);
    }

    FString File1Name = File1->GetBaseName();
    AddInfo(FString::Printf(TEXT("File1: %s"), *File1Name));

    // 3. RotateFile → File1 goes to SendingFiles_
    TSharedPtr<FPersistentFile> File2 = Manager.RotateFile(File1);
    TestNotNull(TEXT("File2 created after rotation"), File2.Get());
    TestFalse(TEXT("File1 should not be Appending"), File1->bIsAppending_);
    TestEqual(TEXT("SendingFiles count should be 1"), Manager.SendingFiles_.Num(), 1);

    // 4. 向 File2 也写入 block，然后 FinishFile
    for (int32 b = 0; b < BlocksPerFile; ++b)
    {
        TArray<uint8> Compressed;
        int64 RawSize = 0;
        FString BId = FString::Printf(TEXT("rot2_block_%d"), b);
        TestTrue(FString::Printf(TEXT("Build block2 %d"), b),
                 BuildCompressedBlock(BId, LogsPerBlock, Compressed, RawSize));
        AllPayloads.Add(Compressed);
        File2->AppendBlock(Compressed, RawSize, LogsPerBlock, true);
    }

    FString File2Name = File2->GetBaseName();
    Manager.FinishFile(File2);
    TestEqual(TEXT("SendingFiles count should be 2"), Manager.SendingFiles_.Num(), 2);

    // 5. CheckSendingFiles → PendingSendCount==0 for both, !AllFinalized → move to recover/
    Manager.CheckSendingFiles();
    TestEqual(TEXT("SendingFiles should be 0 after check"), Manager.SendingFiles_.Num(), 0);

    // Verify files moved to recover/
    TArray<FString> RecoverFiles = ListDatFileBaseNames(Manager.RecoverDir_);
    TestEqual(TEXT("recover/ should have 2 files"), RecoverFiles.Num(), 2);

    // 6. 启动 RecoveryWorker 轮询 recover/
    {
        FRecoveryWorker Worker(InnerConfig, MockHttp, PersistConfig);
        Worker.Start();

        TestTrue(TEXT("Worker should become idle"), WaitForIdle(Worker.bIdle_));
    }

    // 7. 验证 mock 收到了所有 block 的请求
    int32 TotalBlocks = BlocksPerFile * 2;
    int32 ReqCount = MockHttp->RequestCount.Load();
    AddInfo(FString::Printf(TEXT("Mock received %d request(s), expected %d"), ReqCount, TotalBlocks));
    TestEqual(TEXT("Request count == total blocks"), ReqCount, TotalBlocks);

    {
        FScopeLock ScopeLock(&MockHttp->Lock);
        for (int32 i = 0; i < FMath::Min(ReqCount, TotalBlocks); ++i)
        {
            TestEqual(FString::Printf(TEXT("Body %d matches"), i), MockHttp->CapturedBodies[i], AllPayloads[i]);
        }
    }

    // 8. recover/ 中文件应已被 RecoveryWorker 清理
    TestEqual(TEXT("recover/ should be empty"), ListDatFileBaseNames(Manager.RecoverDir_).Num(), 0);

    Manager.CloseAll();
    CleanupTestDir(TestDir);

    AddInfo(TEXT("=== Test 2 (Rotation Recovery) completed ==="));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
