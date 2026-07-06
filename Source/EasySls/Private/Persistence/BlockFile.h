#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"

namespace aliyun
{
namespace sls
{

// ============================================
// Block 文件格式常量
// ============================================

// Block 魔数 'LBK1' (0x4C424B31)
constexpr uint32 BLOCK_MAGIC = 0x4C424B31;

// Block Header 大小
constexpr uint32 BLOCK_HEADER_SIZE = 32;

// 压缩类型标志位
namespace EBlockFlags
{
constexpr uint16 CompressNone = 0;
constexpr uint16 CompressLZ4 = 1;
} // namespace EBlockFlags

/**
 * Block Header - 32 bytes, Little Endian
 */
#pragma pack(push, 1)
struct FBlockHeader
{
    uint32 Magic;       // 4 bytes - 魔数 0x4C424B31 ('LBK1')
    uint32 HeaderSize;  // 4 bytes - Header 大小，固定为 32
    uint32 PayloadSize; // 4 bytes - 压缩后 Payload 大小
    uint32 Crc32;       // 4 bytes - 校验和
    uint16 Flags;       // 2 bytes - 标志位 (bit 0-1: 压缩类型)
    uint16 Reserved1;   // 2 bytes - 对齐保留
    uint64 RawSize;     // 8 bytes - 压缩前原始大小
    uint32 LogCount;    // 4 bytes - 日志条数

    FBlockHeader()
        : Magic(BLOCK_MAGIC),
          HeaderSize(BLOCK_HEADER_SIZE),
          PayloadSize(0),
          Crc32(0),
          Flags(0),
          Reserved1(0),
          RawSize(0),
          LogCount(0)
    {
    }
};
#pragma pack(pop)

static_assert(sizeof(FBlockHeader) == 32, "FBlockHeader must be 32 bytes");

/**
 * 扫描时记录的 Block 信息（Header + 文件偏移）
 */
struct FBlockInfo
{
    int64 Offset;
    FBlockHeader Header;
};

/**
 * FBlockFile - 管理 .dat 文件的读写
 *
 * 职责：
 * - 追加写入 Block（含 Header + Payload）
 * - ScanHeaders：扫描所有 Block Header 并记录偏移（遇到无效 Header 即停止）
 * - ReadPayloadAt：根据已知 FBlockInfo 读取 Payload 并做 CRC 校验
 */
class FBlockFile
{
public:
    FBlockFile() = default;
    ~FBlockFile();

    FBlockFile(const FBlockFile&) = delete;
    FBlockFile& operator=(const FBlockFile&) = delete;

    /** 创建新文件（写模式） */
    bool Create(const FString& Path);

    /** 打开已有文件（读模式） */
    bool OpenForRead(const FString& Path);

    /** 追加写入 Block，返回 Block 索引，失败返回 -1 */
    int32 AppendBlock(const TArray<uint8>& CompressedData, int64 RawSize, int32 LogCount, bool bIsLZ4);

    /**
     * 扫描所有 Block Header，验证 Magic 并记录偏移。
     * 遇到无效 Header 即停止，之后的 Block 不再读取。
     */
    bool ScanHeaders(TArray<FBlockInfo>& OutBlocks);

    /** 根据已知 FBlockInfo 读取 Payload 并做 CRC 校验 */
    bool ReadPayloadAt(const FBlockInfo& Block, TArray<uint8>& OutPayload);

    void Close();

    bool IsOpen() const
    {
        return FileHandle_.IsValid();
    }

    int64 GetWrittenLogBytes() const
    {
        return WrittenBytes_;
    }

    int32 GetBlockCount() const
    {
        return BlockCount_;
    }

    static uint32 CalculateBlockCRC(const FBlockHeader& Header, const uint8* Payload);

private:
    bool EnsureOpenForRead();

    FString FilePath_;
    TUniquePtr<IFileHandle> FileHandle_;
    int32 BlockCount_ = 0;
    int64 WrittenBytes_ = 0;
    bool bIsWriteMode_ = false;
};

} // namespace sls
} // namespace aliyun
