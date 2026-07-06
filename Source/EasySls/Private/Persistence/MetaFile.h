#pragma once

#include "HAL/PlatformFileManager.h"
#include "CoreMinimal.h"

namespace aliyun
{
namespace sls
{

// ============================================
// Meta 文件格式常量
// ============================================

// Meta 魔数 'MET1' (0x4D455431)
constexpr uint32 META_MAGIC = 0x4D455431;

// Meta Header 大小
constexpr uint32 META_HEADER_SIZE = 16;

// Meta 版本号
constexpr uint32 META_VERSION = 1;

/**
 * Meta Header - 16 bytes, Little Endian
 */
#pragma pack(push, 1)
struct FMetaHeader
{
    uint32 Magic;    // 4 bytes - 魔数 0x4D455431 ('MET1')
    uint32 Version;  // 4 bytes - 版本号
    uint64 Reserved; // 8 bytes - 对齐保留

    FMetaHeader() : Magic(META_MAGIC), Version(META_VERSION), Reserved(0) {}
};
#pragma pack(pop)

static_assert(sizeof(FMetaHeader) == 16, "FMetaHeader must be 16 bytes");

/**
 * FMetaFile - 管理 .meta 文件的读写
 *
 * 职责：
 * - 管理 Block 完成状态（Bitmap）
 * - 整体覆盖写入（seek 到开头）
 *
 * 生命周期：
 * - Create()
 * - Load()
 * - Save() 复用已有句柄
 * - Close() 或析构时关闭句柄
 */
class FMetaFile
{
public:
    FMetaFile() = default;
    ~FMetaFile();

    // 禁止拷贝
    FMetaFile(const FMetaFile&) = delete;
    FMetaFile& operator=(const FMetaFile&) = delete;

    /**
     * 创建新文件（保持句柄打开）
     */
    bool Create(const FString& Path);

    /**
     * 加载已有文件
     */
    bool Load(const FString& Path);

    void Close();

    /** 标记 Block 完成并持久化（线程安全） */
    void FinalizeAndSave(int32 BlockIndex);

    /** 扩展 Bitmap 到指定大小（线程安全） */
    void EnsureCapacity(int32 BlockCount);

    /** 设定总 Block 数（FinishAppending / LoadAndValidate 时调用一次，之后不再变化） */
    void SetTotalBlockCount(int32 Count)
    {
        TotalBlockCount_ = Count;
    }

    bool IsFinalized(int32 BlockIndex) const;
    bool AllFinalized() const;

    void Delete();

    bool IsOpen() const
    {
        return FileHandle_.IsValid();
    }

private:
    /** @return true if newly set, false if already finalized */
    bool SetFinalized(int32 BlockIndex);
    bool Save();
    bool EnsureFileHandle();

    FString FilePath_;
    TArray<uint8> Bitmap_;
    TUniquePtr<IFileHandle> FileHandle_;
    FCriticalSection Lock_;

    TAtomic<int32> FinalizedCount_{0};
    int32 TotalBlockCount_{0};
};

} // namespace sls
} // namespace aliyun
