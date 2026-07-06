#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "PersistentFile.h"
#include "PersistentManager.h"

using namespace aliyun::sls;

/**
 * FBlockHeader 结构测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlockHeaderTest,
                                 "EasySls.Unit.PersistentFile.BlockHeader",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBlockHeaderTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("FBlockHeader should be 32 bytes"),
              static_cast<SIZE_T>(sizeof(FBlockHeader)),
              static_cast<SIZE_T>(32));
    TestEqual(TEXT("FMetaHeader should be 16 bytes"),
              static_cast<SIZE_T>(sizeof(FMetaHeader)),
              static_cast<SIZE_T>(16));

    FBlockHeader Header;
    TestEqual(TEXT("Magic should be BLOCK_MAGIC"), Header.Magic, BLOCK_MAGIC);
    TestEqual(TEXT("HeaderSize should be 32"), Header.HeaderSize, BLOCK_HEADER_SIZE);
    TestEqual(TEXT("PayloadSize should be 0"), Header.PayloadSize, 0u);
    TestEqual(TEXT("Crc32 should be 0"), Header.Crc32, 0u);

    return true;
}

/**
 * FPersistentFile 基本操作测试 — Appending → Sending 状态流转
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistentFileBasicTest,
                                 "EasySls.Unit.PersistentFile.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPersistentFileBasicTest::RunTest(const FString& Parameters)
{
    FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestSlsLogs"));
    FString BaseName = TEXT("test_file");

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*TempDir);
    PlatformFile.DeleteFile(*FPaths::Combine(TempDir, BaseName + TEXT(".dat")));
    PlatformFile.DeleteFile(*FPaths::Combine(TempDir, BaseName + TEXT(".meta")));

    auto File = MakeShared<FPersistentFile>(BaseName, TempDir);
    TestTrue(TEXT("InitNew should succeed"), File->InitNew());
    TestTrue(TEXT("Should be Appending"), File->bIsAppending_);
    TestEqual(TEXT("TotalBlockCount should be 0"), File->GetTotalBlockCount(), 0);

    TArray<uint8> Data = {1, 2, 3, 4, 5};
    int32 BlockIndex = File->AppendBlock(Data, 10, 1, true);
    TestEqual(TEXT("First block index should be 0"), BlockIndex, 0);
    TestEqual(TEXT("TotalBlockCount should be 1"), File->GetTotalBlockCount(), 1);
    TestFalse(TEXT("Block should not be finalized yet"), File->IsFinalized(0));

    File->FinishAppending();
    TestFalse(TEXT("Should not be Appending after FinishAppending"), File->bIsAppending_);

    // PendingSendCount starts at 0, IsSendingComplete immediately true
    TestTrue(TEXT("IsSendingComplete should be true (no pending sends)"), File->IsSendingComplete());

    // Finalize block
    File->FinalizeBlock(0);
    TestTrue(TEXT("Block should be finalized"), File->IsFinalized(0));
    TestTrue(TEXT("AllFinalized should be true"), File->AllFinalized());

    // Cleanup
    File->DeleteFiles();

    return true;
}

/**
 * FPersistentFile PendingSendCount 追踪测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistentFileSendTrackingTest,
                                 "EasySls.Unit.PersistentFile.SendTracking",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPersistentFileSendTrackingTest::RunTest(const FString& Parameters)
{
    FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestSlsLogs"));
    FString BaseName = TEXT("test_send_tracking");

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*TempDir);
    PlatformFile.DeleteFile(*FPaths::Combine(TempDir, BaseName + TEXT(".dat")));
    PlatformFile.DeleteFile(*FPaths::Combine(TempDir, BaseName + TEXT(".meta")));

    auto File = MakeShared<FPersistentFile>(BaseName, TempDir);
    TestTrue(TEXT("InitNew"), File->InitNew());

    TArray<uint8> Data = {1, 2, 3};
    File->AppendBlock(Data, 3, 1, true);
    File->AppendBlock(Data, 3, 1, true);
    File->AppendBlock(Data, 3, 1, true);

    // Simulate submitting 3 blocks to Sender
    File->IncrementPendingSend();
    File->IncrementPendingSend();
    File->IncrementPendingSend();
    TestEqual(TEXT("PendingSendCount should be 3"), File->PendingSendCount_.Load(), 3);

    File->FinishAppending();
    TestFalse(TEXT("IsSendingComplete should be false (3 pending)"), File->IsSendingComplete());

    // Simulate Sender callbacks completing
    File->DecrementPendingSend();
    TestEqual(TEXT("PendingSendCount should be 2"), File->PendingSendCount_.Load(), 2);
    TestFalse(TEXT("IsSendingComplete should be false (2 pending)"), File->IsSendingComplete());

    File->DecrementPendingSend();
    File->DecrementPendingSend();
    TestEqual(TEXT("PendingSendCount should be 0"), File->PendingSendCount_.Load(), 0);
    TestTrue(TEXT("IsSendingComplete should be true"), File->IsSendingComplete());

    File->DeleteFiles();

    return true;
}

/**
 * FPersistentFile 多 Block 测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistentFileMultiBlockTest,
                                 "EasySls.Unit.PersistentFile.MultiBlock",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPersistentFileMultiBlockTest::RunTest(const FString& Parameters)
{
    FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestSlsLogs"));
    FString BaseName = TEXT("test_multi_block");

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*TempDir);
    PlatformFile.DeleteFile(*FPaths::Combine(TempDir, BaseName + TEXT(".dat")));
    PlatformFile.DeleteFile(*FPaths::Combine(TempDir, BaseName + TEXT(".meta")));

    auto File = MakeShared<FPersistentFile>(BaseName, TempDir);
    TestTrue(TEXT("InitNew should succeed"), File->InitNew());

    TArray<uint8> Data1 = {1, 2, 3};
    TArray<uint8> Data2 = {4, 5, 6, 7};
    TArray<uint8> Data3 = {8, 9};

    int32 Idx0 = File->AppendBlock(Data1, 3, 1, true);
    int32 Idx1 = File->AppendBlock(Data2, 4, 2, true);
    int32 Idx2 = File->AppendBlock(Data3, 2, 1, true);

    TestEqual(TEXT("Block 0 index"), Idx0, 0);
    TestEqual(TEXT("Block 1 index"), Idx1, 1);
    TestEqual(TEXT("Block 2 index"), Idx2, 2);
    TestEqual(TEXT("TotalBlockCount should be 3"), File->GetTotalBlockCount(), 3);

    // 乱序 finalize
    File->FinalizeBlock(2);
    TestTrue(TEXT("Block 2 should be finalized"), File->IsFinalized(2));
    TestFalse(TEXT("AllFinalized should be false"), File->AllFinalized());

    File->FinalizeBlock(0);
    TestTrue(TEXT("Block 0 should be finalized"), File->IsFinalized(0));
    TestFalse(TEXT("AllFinalized should still be false"), File->AllFinalized());

    File->FinishAppending();
    TestFalse(TEXT("Should not be Appending"), File->bIsAppending_);
    TestFalse(TEXT("AllFinalized should still be false (Block 1 pending)"), File->AllFinalized());

    File->FinalizeBlock(1);
    TestTrue(TEXT("Block 1 should be finalized"), File->IsFinalized(1));
    TestTrue(TEXT("AllFinalized should be true"), File->AllFinalized());

    File->DeleteFiles();

    return true;
}

/**
 * FPersistentFile MoveToDir 测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistentFileMoveTest,
                                 "EasySls.Unit.PersistentFile.MoveToDir",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPersistentFileMoveTest::RunTest(const FString& Parameters)
{
    FString SrcDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestSlsLogs"), TEXT("src"));
    FString DstDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestSlsLogs"), TEXT("dst"));
    FString BaseName = TEXT("test_move_file");

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*SrcDir);
    PlatformFile.CreateDirectoryTree(*DstDir);

    auto File = MakeShared<FPersistentFile>(BaseName, SrcDir);
    TestTrue(TEXT("InitNew"), File->InitNew());

    TArray<uint8> Data = {1, 2, 3};
    File->AppendBlock(Data, 3, 1, true);
    File->FinishAppending();

    // Verify source files exist
    TestTrue(TEXT("Src .dat exists"), PlatformFile.FileExists(*FPaths::Combine(SrcDir, BaseName + TEXT(".dat"))));
    TestTrue(TEXT("Src .meta exists"), PlatformFile.FileExists(*FPaths::Combine(SrcDir, BaseName + TEXT(".meta"))));

    // Move
    TestTrue(TEXT("MoveToDir should succeed"), File->MoveToDir(DstDir));

    // Verify source removed, dest exists
    TestFalse(TEXT("Src .dat should be gone"),
              PlatformFile.FileExists(*FPaths::Combine(SrcDir, BaseName + TEXT(".dat"))));
    TestTrue(TEXT("Dst .dat exists"), PlatformFile.FileExists(*FPaths::Combine(DstDir, BaseName + TEXT(".dat"))));
    TestTrue(TEXT("Dst .meta exists"), PlatformFile.FileExists(*FPaths::Combine(DstDir, BaseName + TEXT(".meta"))));

    // Cleanup
    File->DeleteFiles();

    IFileManager::Get().DeleteDirectory(*SrcDir, false, true);
    IFileManager::Get().DeleteDirectory(*DstDir, false, true);

    return true;
}

/**
 * FPersistentManager 基本测试（temp/recover 目录结构）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistentManagerBasicTest,
                                 "EasySls.Unit.PersistentManager.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPersistentManagerBasicTest::RunTest(const FString& Parameters)
{
    FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestSlsLogs"), TEXT("mgr_basic"));

    FPersistenceConfig Config;
    Config.bEnablePersistence = true;
    Config.PersistenceDir = TestDir;
    Config.MaxFileSize = 1024;
    Config.MaxFileCount = 3;

    FPersistentManager Manager(Config);
    TestTrue(TEXT("Init should succeed"), Manager.Init());
    TestTrue(TEXT("Manager should be enabled"), Manager.IsEnabled());

    // Verify directories created
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    TestTrue(TEXT("temp/ dir exists"), PlatformFile.DirectoryExists(*Manager.TempDir_));
    TestTrue(TEXT("recover/ dir exists"), PlatformFile.DirectoryExists(*Manager.RecoverDir_));

    TSharedPtr<FPersistentFile> File = Manager.CreateActiveFile();
    TestNotNull(TEXT("CreateActiveFile should succeed"), File.Get());

    Manager.FinishFile(File);

    TSharedPtr<FPersistentFile> FileAtLimit = Manager.CreateActiveFile();
    TestNotNull(TEXT("CreateActiveFile should still succeed when reaching MaxFileCount exactly"), FileAtLimit.Get());

    Manager.FinishFile(FileAtLimit);

    TSharedPtr<FPersistentFile> FileMax = Manager.CreateActiveFile();
    TestNotNull(TEXT("CreateActiveFile should allow exactly MaxFileCount active+sending files"), FileMax.Get());

    Manager.FinishFile(FileMax);

    AddExpectedError(TEXT("File count limit reached"), EAutomationExpectedErrorFlags::Contains, 1);
    TSharedPtr<FPersistentFile> FileOverLimit = Manager.CreateActiveFile();
    TestNull(TEXT("CreateActiveFile should fail once active+sending exceeds MaxFileCount"), FileOverLimit.Get());

    Manager.CloseAll();

    IFileManager::Get().DeleteDirectory(*TestDir, false, true);

    return true;
}

/**
 * FPersistentManager CheckSendingFiles 测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistentManagerCheckSendingTest,
                                 "EasySls.Unit.PersistentManager.CheckSendingFiles",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPersistentManagerCheckSendingTest::RunTest(const FString& Parameters)
{
    FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestSlsLogs"), TEXT("mgr_check"));

    FPersistenceConfig Config;
    Config.bEnablePersistence = true;
    Config.PersistenceDir = TestDir;
    Config.MaxFileSize = 1024;
    Config.MaxFileCount = 10;

    FPersistentManager Manager(Config);
    TestTrue(TEXT("Init"), Manager.Init());

    // Create and rotate a file (all blocks finalized → should be deleted)
    TSharedPtr<FPersistentFile> File1 = Manager.CreateActiveFile();
    TArray<uint8> Data = {1, 2, 3};
    File1->AppendBlock(Data, 3, 1, true);
    File1->FinalizeBlock(0);
    FString File1Name = File1->GetBaseName();

    TSharedPtr<FPersistentFile> File2 = Manager.RotateFile(File1);
    TestEqual(TEXT("SendingFiles should have 1"), Manager.SendingFiles_.Num(), 1);

    // File1 has PendingSendCount==0 and AllFinalized → CheckSendingFiles deletes it
    Manager.CheckSendingFiles();
    TestEqual(TEXT("SendingFiles should be 0 after check"), Manager.SendingFiles_.Num(), 0);

    // Create and rotate a file with unfinalized block → should move to recover/
    TSharedPtr<FPersistentFile> File3 = Manager.CreateActiveFile();
    File3->AppendBlock(Data, 3, 1, true);
    FString File3Name = File3->GetBaseName();

    Manager.FinishFile(File3);
    TestEqual(TEXT("SendingFiles should have 1"), Manager.SendingFiles_.Num(), 1);

    Manager.CheckSendingFiles();
    TestEqual(TEXT("SendingFiles should be 0 after check"), Manager.SendingFiles_.Num(), 0);

    // Verify file was moved to recover/
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    TestTrue(TEXT("File3 .dat should be in recover/"),
             PlatformFile.FileExists(*FPaths::Combine(Manager.RecoverDir_, File3Name + TEXT(".dat"))));

    Manager.CloseAll();
    IFileManager::Get().DeleteDirectory(*TestDir, false, true);

    return true;
}

/**
 * CRC32 计算测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrc32CalculationTest,
                                 "EasySls.Unit.PersistentFile.CRC32",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCrc32CalculationTest::RunTest(const FString& Parameters)
{
    FBlockHeader Header;
    Header.PayloadSize = 5;
    Header.Flags = EBlockFlags::CompressLZ4;
    Header.RawSize = 10;
    Header.LogCount = 1;

    TArray<uint8> Payload = {1, 2, 3, 4, 5};

    uint32 Crc1 = FBlockFile::CalculateBlockCRC(Header, Payload.GetData());
    uint32 Crc2 = FBlockFile::CalculateBlockCRC(Header, Payload.GetData());

    TestEqual(TEXT("CRC should be consistent"), Crc1, Crc2);
    TestTrue(TEXT("CRC should not be 0"), Crc1 != 0);

    Payload[0] = 99;
    uint32 Crc3 = FBlockFile::CalculateBlockCRC(Header, Payload.GetData());
    TestTrue(TEXT("CRC should differ with different payload"), Crc1 != Crc3);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
