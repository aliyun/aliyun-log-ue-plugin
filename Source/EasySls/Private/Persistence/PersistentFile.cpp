#include "PersistentFile.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "InnerLog.h"

namespace aliyun
{
namespace sls
{

FPersistentFile::FPersistentFile(const FString& InBaseName, const FString& InDir) : BaseName_(InBaseName), Dir_(InDir)
{
    DatFilePath_ = FPaths::Combine(Dir_, BaseName_ + TEXT(".dat"));
    MetaFilePath_ = FPaths::Combine(Dir_, BaseName_ + TEXT(".meta"));
    CreateTime_ = FDateTime::UtcNow().ToUnixTimestamp();
}

FPersistentFile::~FPersistentFile()
{
    Close();
}

FString FPersistentFile::GenerateBaseName()
{
    int64 Timestamp = FDateTime::UtcNow().ToUnixTimestamp();
    FString GuidPart = FGuid::NewGuid().ToString(EGuidFormats::Short);
    return FString::Printf(TEXT("sls_log_%lld_%s"), Timestamp, *GuidPart);
}

// ==========================================
// Lifecycle
// ==========================================

bool FPersistentFile::InitNew()
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*Dir_))
    {
        if (!PlatformFile.CreateDirectoryTree(*Dir_))
        {
            SLS_LOG_ERROR("Failed to create directory: %s", *Dir_);
            return false;
        }
    }

    if (!BlockFile_.Create(DatFilePath_))
    {
        return false;
    }

    if (!MetaFile_.Create(MetaFilePath_))
    {
        BlockFile_.Close();
        return false;
    }

    TotalBlockCount_ = 0;
    bIsAppending_ = true;
    SLS_LOG_INFO("Created new persistent file: %s", *BaseName_);
    return true;
}

bool FPersistentFile::LoadAndValidate()
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.FileExists(*DatFilePath_) || !PlatformFile.FileExists(*MetaFilePath_))
    {
        SLS_LOG_WARNING("Missing dat or meta file for: %s", *BaseName_);
        return false;
    }

    // 从文件名解析创建时间: sls_log_{timestamp}_{guid}
    FString NamePart = FPaths::GetBaseFilename(BaseName_);
    TArray<FString> Parts;
    NamePart.ParseIntoArray(Parts, TEXT("_"));
    if (Parts.Num() >= 3)
    {
        CreateTime_ = FCString::Atoi64(*Parts[2]);
    }

    if (!BlockFile_.OpenForRead(DatFilePath_))
    {
        return false;
    }

    if (!BlockFile_.ScanHeaders(Blocks_))
    {
        SLS_LOG_WARNING("Failed to scan block headers: %s", *DatFilePath_);
        BlockFile_.Close();
        return false;
    }

    TotalBlockCount_ = Blocks_.Num();

    if (!MetaFile_.Load(MetaFilePath_))
    {
        SLS_LOG_WARNING("Failed to load meta file: %s", *MetaFilePath_);
        BlockFile_.Close();
        Blocks_.Empty();
        TotalBlockCount_ = 0;
        return false;
    }

    MetaFile_.EnsureCapacity(TotalBlockCount_);
    MetaFile_.SetTotalBlockCount(TotalBlockCount_);
    bIsAppending_ = false;

    SLS_LOG_INFO("Loaded persistent file: %s (blocks=%d)", *BaseName_, TotalBlockCount_);
    return true;
}

// ==========================================
// Operations
// ==========================================

int32 FPersistentFile::AppendBlock(const TArray<uint8>& CompressedData, int64 RawSize, int32 LogCount, bool bIsLZ4)
{
    if (!bIsAppending_)
    {
        SLS_LOG_ERROR("Cannot append block: file not in Appending state");
        return -1;
    }

    int32 BlockIndex = BlockFile_.AppendBlock(CompressedData, RawSize, LogCount, bIsLZ4);
    if (BlockIndex < 0)
    {
        return -1;
    }

    TotalBlockCount_++;
    MetaFile_.EnsureCapacity(TotalBlockCount_);

    return BlockIndex;
}

void FPersistentFile::FinalizeBlock(int32 BlockIndex)
{
    if (BlockIndex < 0)
    {
        SLS_LOG_WARNING("Invalid block index: %d", BlockIndex);
        return;
    }

    MetaFile_.FinalizeAndSave(BlockIndex);
    SLS_LOG_DEBUG("Finalized block %d", BlockIndex);
}

const FBlockHeader& FPersistentFile::GetBlockHeader(int32 BlockIndex) const
{
    return Blocks_[BlockIndex].Header;
}

bool FPersistentFile::ReadBlockPayload(int32 BlockIndex, TArray<uint8>& OutPayload)
{
    if (BlockIndex < 0 || BlockIndex >= Blocks_.Num())
    {
        SLS_LOG_ERROR("ReadBlockPayload: invalid index %d (total=%d)", BlockIndex, Blocks_.Num());
        return false;
    }
    return BlockFile_.ReadPayloadAt(Blocks_[BlockIndex], OutPayload);
}

// ==========================================
// State
// ==========================================

void FPersistentFile::FinishAppending()
{
    if (!bIsAppending_)
    {
        return;
    }

    bIsAppending_ = false;
    MetaFile_.SetTotalBlockCount(TotalBlockCount_);
    BlockFile_.Close();
    SLS_LOG_INFO("File %s: Appending -> Sending, dat file closed (blocks=%d)", *BaseName_, TotalBlockCount_);
}

// ==========================================
// Query
// ==========================================

bool FPersistentFile::IsFinalized(int32 BlockIndex) const
{
    return MetaFile_.IsFinalized(BlockIndex);
}

bool FPersistentFile::AllFinalized() const
{
    return MetaFile_.AllFinalized();
}

int64 FPersistentFile::GetWrittenLogBytes() const
{
    return BlockFile_.GetWrittenLogBytes();
}

// ==========================================
// File Operations
// ==========================================

bool FPersistentFile::MoveToDir(const FString& NewDir)
{
    Close();

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    FString NewDatPath = FPaths::Combine(NewDir, BaseName_ + TEXT(".dat"));
    FString NewMetaPath = FPaths::Combine(NewDir, BaseName_ + TEXT(".meta"));

    if (!PlatformFile.MoveFile(*NewMetaPath, *MetaFilePath_))
    {
        SLS_LOG_ERROR("Failed to move meta file: %s -> %s", *MetaFilePath_, *NewMetaPath);
        return false;
    }

    if (!PlatformFile.MoveFile(*NewDatPath, *DatFilePath_))
    {
        SLS_LOG_ERROR("Failed to move dat file: %s -> %s", *DatFilePath_, *NewDatPath);
        return false;
    }

    Dir_ = NewDir;
    DatFilePath_ = NewDatPath;
    MetaFilePath_ = NewMetaPath;

    SLS_LOG_INFO("Moved file %s to %s", *BaseName_, *NewDir);
    return true;
}

void FPersistentFile::DeleteFiles()
{
    Close();

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (PlatformFile.FileExists(*DatFilePath_))
    {
        PlatformFile.DeleteFile(*DatFilePath_);
    }

    MetaFile_.Delete();

    SLS_LOG_INFO("Deleted persistent file: %s", *BaseName_);
}

void FPersistentFile::Close()
{
    BlockFile_.Close();
    MetaFile_.Close();
}

} // namespace sls
} // namespace aliyun
