#include "MetaFile.h"
#include "Misc/FileHelper.h"
#include "InnerLog.h"

namespace aliyun
{
namespace sls
{

FMetaFile::~FMetaFile()
{
    Close();
}

bool FMetaFile::Create(const FString& Path)
{
    FilePath_ = Path;
    Bitmap_.Empty();
    return Save();
}

bool FMetaFile::Load(const FString& Path)
{
    FilePath_ = Path;

    // 一次性读取整个文件
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath_))
    {
        SLS_LOG_ERROR("Failed to load meta file: %s", *FilePath_);
        return false;
    }

    // 检查文件大小是否至少包含 Header
    if (FileData.Num() < sizeof(FMetaHeader))
    {
        SLS_LOG_ERROR("Meta file too small: %d bytes", FileData.Num());
        return false;
    }

    // 解析 Header
    FMetaHeader Header;
    FMemory::Memcpy(&Header, FileData.GetData(), sizeof(FMetaHeader));

    // 校验 Magic
    if (Header.Magic != META_MAGIC)
    {
        SLS_LOG_ERROR("Invalid meta magic: 0x%08X", Header.Magic);
        return false;
    }

    // 校验版本
    if (Header.Version != META_VERSION)
    {
        SLS_LOG_WARNING("Meta version mismatch: %d vs %d", Header.Version, META_VERSION);
        return false;
    }

    // 提取 Bitmap（Header 之后的所有数据）
    int32 BitmapSize = FileData.Num() - sizeof(FMetaHeader);
    if (BitmapSize > 0)
    {
        Bitmap_.SetNumUninitialized(BitmapSize);
        FMemory::Memcpy(Bitmap_.GetData(), FileData.GetData() + sizeof(FMetaHeader), BitmapSize);
    }
    else
    {
        Bitmap_.Empty();
    }

    // 统计已 finalized 的 Block 数量
    int32 Count = 0;
    for (int32 ByteIdx = 0; ByteIdx < Bitmap_.Num(); ++ByteIdx)
    {
        uint8 Byte = Bitmap_[ByteIdx];
        while (Byte)
        {
            Count += Byte & 1;
            Byte >>= 1;
        }
    }
    FinalizedCount_.Store(Count);

    return true;
}

bool FMetaFile::Save()
{
    if (!EnsureFileHandle())
    {
        return false;
    }

    // 计算总大小并提前分配内存
    int32 TotalSize = sizeof(FMetaHeader) + Bitmap_.Num();
    TArray<uint8> FileData;
    FileData.SetNumUninitialized(TotalSize);

    // 序列化 Header
    FMetaHeader Header;
    FMemory::Memcpy(FileData.GetData(), &Header, sizeof(FMetaHeader));

    // 序列化 Bitmap
    if (Bitmap_.Num() > 0)
    {
        FMemory::Memcpy(FileData.GetData() + sizeof(FMetaHeader), Bitmap_.GetData(), Bitmap_.Num());
    }

    // Seek 到文件开头
    if (!FileHandle_->Seek(0))
    {
        SLS_LOG_ERROR("Failed to seek meta file to beginning");
        return false;
    }

    // 一次性写入所有数据
    if (!FileHandle_->Write(FileData.GetData(), TotalSize))
    {
        SLS_LOG_ERROR("Failed to write meta file");
        return false;
    }

    // 如果文件大小大于需要的大小，截断文件（防止旧数据残留）
    int64 CurrentFileSize = FileHandle_->Size();
    if (CurrentFileSize > TotalSize)
    {
        if (!FileHandle_->Truncate(TotalSize))
        {
            // Truncate 失败不是致命错误，只记录警告
            SLS_LOG_WARNING("Failed to truncate meta file");
        }
    }

    FileHandle_->Flush();
    return true;
}

void FMetaFile::Close()
{
    FileHandle_.Reset();
}

void FMetaFile::FinalizeAndSave(int32 BlockIndex)
{
    FScopeLock ScopeLock(&Lock_);
    if (SetFinalized(BlockIndex))
    {
        FinalizedCount_.IncrementExchange();
    }
    Save();
}

bool FMetaFile::SetFinalized(int32 BlockIndex)
{
    int32 RequiredBytes = (BlockIndex + 1 + 7) / 8;
    if (Bitmap_.Num() < RequiredBytes)
    {
        Bitmap_.SetNumZeroed(RequiredBytes);
    }

    int32 ByteIndex = BlockIndex / 8;
    int32 BitIndex = BlockIndex % 8;
    if (Bitmap_[ByteIndex] & (1 << BitIndex))
    {
        return false;
    }
    Bitmap_[ByteIndex] |= (1 << BitIndex);
    return true;
}

bool FMetaFile::IsFinalized(int32 BlockIndex) const
{
    int32 ByteIndex = BlockIndex / 8;
    int32 BitIndex = BlockIndex % 8;

    if (ByteIndex >= Bitmap_.Num())
    {
        return false;
    }

    return (Bitmap_[ByteIndex] & (1 << BitIndex)) != 0;
}

bool FMetaFile::AllFinalized() const
{
    return TotalBlockCount_ > 0 && FinalizedCount_.Load() >= TotalBlockCount_;
}

void FMetaFile::EnsureCapacity(int32 BlockCount)
{
    FScopeLock ScopeLock(&Lock_);
    int32 RequiredBytes = (BlockCount + 7) / 8;
    if (Bitmap_.Num() < RequiredBytes)
    {
        Bitmap_.SetNumZeroed(RequiredBytes);
    }
}

void FMetaFile::Delete()
{
    Close();

    if (!FilePath_.IsEmpty())
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.DeleteFile(*FilePath_);
    }
}

bool FMetaFile::EnsureFileHandle()
{
    if (FileHandle_)
    {
        return true;
    }

    if (FilePath_.IsEmpty())
    {
        SLS_LOG_ERROR("MetaFile path is empty");
        return false;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FileHandle_.Reset(PlatformFile.OpenWrite(*FilePath_, false, true));

    if (!FileHandle_)
    {
        SLS_LOG_ERROR("Failed to open meta file: %s", *FilePath_);
        return false;
    }

    return true;
}

} // namespace sls
} // namespace aliyun
