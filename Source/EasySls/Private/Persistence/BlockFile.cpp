#include "BlockFile.h"
#include "Misc/CRC.h"
#include "InnerLog.h"

namespace aliyun
{
namespace sls
{

FBlockFile::~FBlockFile()
{
    Close();
}

bool FBlockFile::Create(const FString& Path)
{
    FilePath_ = Path;
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    FileHandle_.Reset(PlatformFile.OpenWrite(*FilePath_, true, false));
    if (!FileHandle_)
    {
        SLS_LOG_ERROR("Failed to create block file: %s", *FilePath_);
        return false;
    }

    BlockCount_ = 0;
    WrittenBytes_ = 0;
    bIsWriteMode_ = true;
    return true;
}

bool FBlockFile::OpenForRead(const FString& Path)
{
    FilePath_ = Path;
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    FileHandle_.Reset(PlatformFile.OpenRead(*FilePath_));
    if (!FileHandle_)
    {
        SLS_LOG_ERROR("Failed to open block file for read: %s", *FilePath_);
        return false;
    }

    bIsWriteMode_ = false;
    return true;
}

bool FBlockFile::EnsureOpenForRead()
{
    if (FileHandle_.IsValid())
    {
        return true;
    }
    return OpenForRead(FilePath_);
}

int32 FBlockFile::AppendBlock(const TArray<uint8>& CompressedData, int64 RawSize, int32 LogCount, bool bIsLZ4)
{
    if (!FileHandle_ || !bIsWriteMode_)
    {
        SLS_LOG_ERROR("BlockFile not in write mode");
        return -1;
    }

    FBlockHeader Header;
    Header.PayloadSize = CompressedData.Num();
    Header.Flags = bIsLZ4 ? EBlockFlags::CompressLZ4 : EBlockFlags::CompressNone;
    Header.RawSize = RawSize;
    Header.LogCount = LogCount;
    Header.Crc32 = CalculateBlockCRC(Header, CompressedData.GetData());

    if (!FileHandle_->Write(reinterpret_cast<const uint8*>(&Header), sizeof(FBlockHeader)))
    {
        SLS_LOG_ERROR("Failed to write block header");
        return -1;
    }

    if (!FileHandle_->Write(CompressedData.GetData(), CompressedData.Num()))
    {
        SLS_LOG_ERROR("Failed to write block payload");
        return -1;
    }

    FileHandle_->Flush();

    int32 BlockIndex = BlockCount_;
    BlockCount_++;
    WrittenBytes_ += CompressedData.Num();

    SLS_LOG_DEBUG("Appended block %d: PayloadSize=%d, LogCount=%d", BlockIndex, CompressedData.Num(), LogCount);
    return BlockIndex;
}

bool FBlockFile::ScanHeaders(TArray<FBlockInfo>& OutBlocks)
{
    OutBlocks.Empty();

    if (!EnsureOpenForRead())
    {
        SLS_LOG_ERROR("BlockFile %s failed to ensure open for read", *FilePath_);
        return false;
    }

    int64 FileSize = FileHandle_->Size();
    int64 CurrentOffset = 0;

    if (!FileHandle_->Seek(0))
    {
        return false;
    }

    while (CurrentOffset + static_cast<int64>(sizeof(FBlockHeader)) <= FileSize)
    {
        FBlockHeader Header;
        if (!FileHandle_->Read(reinterpret_cast<uint8*>(&Header), sizeof(FBlockHeader)))
        {
            break;
        }

        if (Header.Magic != BLOCK_MAGIC)
        {
            SLS_LOG_WARNING("Invalid block magic at offset %lld, stopping scan", CurrentOffset);
            break;
        }

        int64 BlockEnd = CurrentOffset + sizeof(FBlockHeader) + Header.PayloadSize;
        if (BlockEnd > FileSize)
        {
            SLS_LOG_WARNING("Block at offset %lld exceeds file size, stopping scan", CurrentOffset);
            break;
        }

        FBlockInfo Info;
        Info.Offset = CurrentOffset;
        Info.Header = Header;
        OutBlocks.Add(Info);

        if (!FileHandle_->Seek(BlockEnd))
        {
            break;
        }
        CurrentOffset = BlockEnd;
    }

    SLS_LOG_DEBUG("ScanHeaders: found %d valid blocks in %s", OutBlocks.Num(), *FilePath_);
    return true;
}

bool FBlockFile::ReadPayloadAt(const FBlockInfo& Block, TArray<uint8>& OutPayload)
{
    if (!EnsureOpenForRead())
    {
        SLS_LOG_ERROR("BlockFile %s failed to ensure open for read", *FilePath_);
        return false;
    }

    int64 PayloadOffset = Block.Offset + sizeof(FBlockHeader);
    if (!FileHandle_->Seek(PayloadOffset))
    {
        SLS_LOG_ERROR("Failed to seek to payload at offset %lld", PayloadOffset);
        return false;
    }

    OutPayload.SetNumUninitialized(Block.Header.PayloadSize);
    if (!FileHandle_->Read(OutPayload.GetData(), Block.Header.PayloadSize))
    {
        SLS_LOG_ERROR("Failed to read payload at offset %lld", Block.Offset);
        return false;
    }

    uint32 ExpectedCrc = CalculateBlockCRC(Block.Header, OutPayload.GetData());
    if (Block.Header.Crc32 != ExpectedCrc)
    {
        SLS_LOG_ERROR("CRC mismatch at offset %lld (expected 0x%08X, got 0x%08X)",
                      Block.Offset,
                      ExpectedCrc,
                      Block.Header.Crc32);
        return false;
    }

    return true;
}

void FBlockFile::Close()
{
    FileHandle_.Reset();
    bIsWriteMode_ = false;
}

uint32 FBlockFile::CalculateBlockCRC(const FBlockHeader& Header, const uint8* Payload)
{
    FBlockHeader Temp = Header;
    Temp.Crc32 = 0;

    uint32 Crc = 0;
    Crc = FCrc::MemCrc32(&Temp, sizeof(FBlockHeader), Crc);
    if (Payload && Header.PayloadSize > 0)
    {
        Crc = FCrc::MemCrc32(Payload, Header.PayloadSize, Crc);
    }
    return Crc;
}

} // namespace sls
} // namespace aliyun
