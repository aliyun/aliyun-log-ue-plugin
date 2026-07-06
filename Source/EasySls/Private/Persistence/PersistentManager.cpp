#include "PersistentManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "InnerLog.h"

namespace aliyun
{
namespace sls
{

FPersistentManager::FPersistentManager(const FPersistenceConfig& InConfig) : Config_(InConfig)
{
    TempDir_ = FPaths::Combine(Config_.PersistenceDir, TEXT("temp"));
    RecoverDir_ = FPaths::Combine(Config_.PersistenceDir, TEXT("recover"));
}

FPersistentManager::~FPersistentManager()
{
    CloseAll();
}

bool FPersistentManager::Init()
{
    if (!Config_.bEnablePersistence)
    {
        SLS_LOG_INFO("Persistence disabled");
        return true;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.CreateDirectoryTree(*TempDir_))
    {
        SLS_LOG_ERROR("Failed to create temp directory: %s", *TempDir_);
        return false;
    }

    if (!PlatformFile.CreateDirectoryTree(*RecoverDir_))
    {
        SLS_LOG_ERROR("Failed to create recover directory: %s", *RecoverDir_);
        return false;
    }

    // 启动时将 temp/ 中的残留文件移到 recover/（上次 crash 的遗留）
    TArray<FString> TempFiles = ListLogFiles(TempDir_);
    for (const FString& BaseName : TempFiles)
    {
        FString SrcDat = FPaths::Combine(TempDir_, BaseName + TEXT(".dat"));
        FString SrcMeta = FPaths::Combine(TempDir_, BaseName + TEXT(".meta"));
        FString DstDat = FPaths::Combine(RecoverDir_, BaseName + TEXT(".dat"));
        FString DstMeta = FPaths::Combine(RecoverDir_, BaseName + TEXT(".meta"));

        PlatformFile.MoveFile(*DstMeta, *SrcMeta);
        PlatformFile.MoveFile(*DstDat, *SrcDat);
        SLS_LOG_INFO("Startup: moved %s from temp/ to recover/", *BaseName);
    }

    CleanupOnStartup();

    SLS_LOG_INFO("PersistentManager initialized: TempDir=%s, RecoverDir=%s, MaxFileSize=%lld, MaxFileCount=%d",
                 *TempDir_,
                 *RecoverDir_,
                 Config_.MaxFileSize,
                 Config_.MaxFileCount);
    return true;
}

TSharedPtr<FPersistentFile> FPersistentManager::CreateActiveFile()
{
    if (!Config_.bEnablePersistence)
    {
        return nullptr;
    }

    const int32 ActiveAndSendingFileCount = SendingFiles_.Num() + 1;
    if (ActiveAndSendingFileCount > Config_.MaxFileCount)
    {
        SLS_LOG_ERROR("File count limit reached (%d active+sending > %d), cannot create new file",
                      ActiveAndSendingFileCount,
                      Config_.MaxFileCount);
        return nullptr;
    }

    FString BaseName = FPersistentFile::GenerateBaseName();
    auto NewFile = MakeShared<FPersistentFile>(BaseName, TempDir_);

    if (!NewFile->InitNew())
    {
        SLS_LOG_ERROR("Failed to create new persistent file: %s", *BaseName);
        return nullptr;
    }

    return NewFile;
}

TSharedPtr<FPersistentFile> FPersistentManager::RotateFile(TSharedPtr<FPersistentFile> CurrentFile)
{
    if (!Config_.bEnablePersistence)
    {
        return nullptr;
    }

    if (CurrentFile.IsValid())
    {
        FinishFile(CurrentFile);
    }

    return CreateActiveFile();
}

void FPersistentManager::FinishFile(TSharedPtr<FPersistentFile> File)
{
    if (!File.IsValid())
    {
        return;
    }

    File->FinishAppending();
    SendingFiles_.Add(File);
    SLS_LOG_INFO("File %s: added to SendingFiles (total: %d)", *File->GetBaseName(), SendingFiles_.Num());
}

void FPersistentManager::CheckSendingFiles()
{
    for (int32 i = SendingFiles_.Num() - 1; i >= 0; --i)
    {
        auto& File = SendingFiles_[i];
        if (!File.IsValid() || !File->IsSendingComplete())
        {
            continue;
        }

        if (File->AllFinalized())
        {
            SLS_LOG_INFO("File %s: all blocks finalized, deleting", *File->GetBaseName());
            File->DeleteFiles();
        }
        else
        {
            SLS_LOG_INFO("File %s: has unfinalized blocks, moving to recover/", *File->GetBaseName());
            File->MoveToDir(RecoverDir_);
        }

        SendingFiles_.RemoveAt(i);
    }
}

bool FPersistentManager::ShouldRotate(TSharedPtr<FPersistentFile> CurrentFile) const
{
    if (!CurrentFile.IsValid())
    {
        return false;
    }

    return CurrentFile->GetWrittenLogBytes() >= Config_.MaxFileSize;
}

void FPersistentManager::CloseAll()
{
    for (auto& File : SendingFiles_)
    {
        if (File.IsValid())
        {
            File->Close();
        }
    }
    SendingFiles_.Empty();
}

void FPersistentManager::CleanupOnStartup()
{
    TArray<FString> RecoverFiles = ListLogFiles(RecoverDir_);

    if (RecoverFiles.Num() <= Config_.MaxFileCount)
    {
        return;
    }

    RecoverFiles.Sort(
        [](const FString& A, const FString& B)
        {
            TArray<FString> PartsA, PartsB;
            A.ParseIntoArray(PartsA, TEXT("_"));
            B.ParseIntoArray(PartsB, TEXT("_"));

            int64 TimeA = PartsA.Num() >= 3 ? FCString::Atoi64(*PartsA[2]) : 0;
            int64 TimeB = PartsB.Num() >= 3 ? FCString::Atoi64(*PartsB[2]) : 0;
            return TimeA < TimeB;
        });

    int32 ToDelete = RecoverFiles.Num() - Config_.MaxFileCount;
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    for (int32 i = 0; i < ToDelete; i++)
    {
        FString DatPath = FPaths::Combine(RecoverDir_, RecoverFiles[i] + TEXT(".dat"));
        FString MetaPath = FPaths::Combine(RecoverDir_, RecoverFiles[i] + TEXT(".meta"));

        PlatformFile.DeleteFile(*DatPath);
        PlatformFile.DeleteFile(*MetaPath);

        SLS_LOG_INFO("Cleanup on startup: deleted old file %s", *RecoverFiles[i]);
    }
}

TArray<FString> FPersistentManager::ListLogFiles(const FString& Dir) const
{
    TArray<FString> Result;

    TArray<FString> FoundFiles;
    FString SearchPath = FPaths::Combine(Dir, TEXT("sls_log_*.dat"));
    IFileManager::Get().FindFiles(FoundFiles, *SearchPath, true, false);

    for (const FString& FileName : FoundFiles)
    {
        Result.Add(FPaths::GetBaseFilename(FileName));
    }

    return Result;
}

} // namespace sls
} // namespace aliyun
