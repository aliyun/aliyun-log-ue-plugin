#pragma once

#include "BlockFile.h"
#include "CoreMinimal.h"
#include "MetaFile.h"

#if WITH_DEV_AUTOMATION_TESTS
class FPersistentFileBasicTest;
class FPersistentFileSendTrackingTest;
class FPersistentFileMultiBlockTest;
class FPersistenceAndRecoveryTest;
class FRotationRecoveryTest;
#endif

namespace aliyun
{
namespace sls
{

/**
 * FPersistentFile - 封装一对持久化文件（.dat + .meta）
 *
 * 所有权模型：
 * - Appending：Flusher 独占写入，Sender 异步发送
 * - 非 Appending：不再写入，等所有 SendTask 完成后由 Flusher 轮询决定 delete 或 move to recover/
 *
 * PendingSendCount 跟踪 in-flight 的 SendTask 数量（原子计数器，线程安全）
 */
class FPersistentFile : public TSharedFromThis<FPersistentFile>
{
#if WITH_DEV_AUTOMATION_TESTS
    friend class ::FPersistentFileBasicTest;
    friend class ::FPersistentFileSendTrackingTest;
    friend class ::FPersistentFileMultiBlockTest;
    friend class ::FPersistenceAndRecoveryTest;
    friend class ::FRotationRecoveryTest;
#endif

public:
    FPersistentFile(const FString& InBaseName, const FString& InDir);
    ~FPersistentFile();

    FPersistentFile(const FPersistentFile&) = delete;
    FPersistentFile& operator=(const FPersistentFile&) = delete;

    // ==========================================
    // Lifecycle
    // ==========================================

    /** 创建新文件，进入 Appending 状态 */
    bool InitNew();

    /** 加载并校验已有文件（Recovery 使用） */
    bool LoadAndValidate();

    // ==========================================
    // Operations
    // ==========================================

    /** 追加写入 Block（仅 Appending 状态），返回 Block 索引，失败返回 -1 */
    int32 AppendBlock(const TArray<uint8>& CompressedData, int64 RawSize, int32 LogCount, bool bIsLZ4 = true);

    /** 标记 Block 完成（仅更新 bitmap，不触发清理） */
    void FinalizeBlock(int32 BlockIndex);

    /** 获取 Block Header（LoadAndValidate 后有效） */
    const FBlockHeader& GetBlockHeader(int32 BlockIndex) const;

    /** 读取 Block Payload 并做 CRC 校验（LoadAndValidate 后有效） */
    bool ReadBlockPayload(int32 BlockIndex, TArray<uint8>& OutPayload);

    // ==========================================
    // State
    // ==========================================

    /** 停止 appending，关闭 .dat 写句柄 */
    void FinishAppending();

    // ==========================================
    // Send Tracking
    // ==========================================

    void IncrementPendingSend()
    {
        PendingSendCount_.IncrementExchange();
    }

    void DecrementPendingSend()
    {
        PendingSendCount_.DecrementExchange();
    }

    /** 所有 SendTask 都已完成（计数器归零） */
    bool IsSendingComplete() const
    {
        return !bIsAppending_ && PendingSendCount_.Load() == 0;
    }

    // ==========================================
    // Query
    // ==========================================

    bool IsFinalized(int32 BlockIndex) const;
    bool AllFinalized() const;

    int32 GetTotalBlockCount() const
    {
        return TotalBlockCount_;
    }

    int64 GetWrittenLogBytes() const;

    int64 GetCreateTime() const
    {
        return CreateTime_;
    }

    const FString& GetBaseName() const
    {
        return BaseName_;
    }

    // ==========================================
    // File Operations
    // ==========================================

    /** 关闭所有句柄，将 .dat 和 .meta 移动到指定目录 */
    bool MoveToDir(const FString& NewDir);

    /** 删除 .dat 和 .meta 文件 */
    void DeleteFiles();

    /** 关闭所有文件句柄 */
    void Close();

    // ==========================================
    // Static Helpers
    // ==========================================

    static FString GenerateBaseName();

private:
    FString BaseName_;
    FString Dir_;
    FString DatFilePath_;
    FString MetaFilePath_;
    int64 CreateTime_ = 0;

    FBlockFile BlockFile_;
    FMetaFile MetaFile_;

    TArray<FBlockInfo> Blocks_;
    int32 TotalBlockCount_ = 0;
    bool bIsAppending_ = true;
    TAtomic<int32> PendingSendCount_{0};
};

} // namespace sls
} // namespace aliyun
