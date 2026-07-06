#pragma once

#include "CoreMinimal.h"
#include "PersistentFile.h"

#if WITH_DEV_AUTOMATION_TESTS
class FPersistentManagerBasicTest;
class FPersistentManagerCheckSendingTest;
class FRotationRecoveryTest;
#endif

namespace aliyun
{
namespace sls
{

struct FPersistenceConfig
{
    bool bEnablePersistence = true;
    FString PersistenceDir;
    int64 MaxFileSize = 10 * 1024 * 1024;
    int32 MaxFileCount = 10;
    int64 MaxPersistenceTimeSec = 7 * 24 * 3600;
};

/**
 * FPersistentManager - 管理持久化文件的生命周期
 *
 * 目录结构：
 * - {PersistenceDir}/temp/    活跃文件 + Sending 状态文件
 * - {PersistenceDir}/recover/ 待恢复文件（RecoveryWorker 轮询处理）
 *
 * 职责：
 * - 在 temp/ 中创建和管理 PersistentFile
 * - 文件轮转（FinishAppending，加入 SendingFiles_）
 * - CheckSendingFiles：PendingSendCount==0 时决定 delete 或 move to recover/
 * - 启动时清理超限文件
 */
class FPersistentManager
{
#if WITH_DEV_AUTOMATION_TESTS
    friend class ::FPersistentManagerBasicTest;
    friend class ::FPersistentManagerCheckSendingTest;
    friend class ::FRotationRecoveryTest;
#endif

public:
    explicit FPersistentManager(const FPersistenceConfig& InConfig);
    ~FPersistentManager();

    FPersistentManager(const FPersistentManager&) = delete;
    FPersistentManager& operator=(const FPersistentManager&) = delete;

    bool Init();

    /** 创建新的活跃文件（在 temp/ 目录） */
    TSharedPtr<FPersistentFile> CreateActiveFile();

    /**
     * 轮转文件：旧文件 FinishAppending → SendingFiles_，创建新活跃文件
     */
    TSharedPtr<FPersistentFile> RotateFile(TSharedPtr<FPersistentFile> CurrentFile);

    /**
     * 将文件加入 SendingFiles_（用于 Flusher::Stop 时的 ActiveFile）
     */
    void FinishFile(TSharedPtr<FPersistentFile> File);

    /**
     * 轮询 SendingFiles_，对 PendingSendCount==0 的文件执行 delete 或 move to recover
     * 由 Flusher 在每次 ProcessLogGroup 后调用
     */
    void CheckSendingFiles();

    bool ShouldRotate(TSharedPtr<FPersistentFile> CurrentFile) const;

    void CloseAll();

    bool IsEnabled() const
    {
        return Config_.bEnablePersistence;
    }

private:
    void CleanupOnStartup();
    TArray<FString> ListLogFiles(const FString& Dir) const;

    FPersistenceConfig Config_;
    FString TempDir_;
    FString RecoverDir_;
    TArray<TSharedPtr<FPersistentFile>> SendingFiles_;
};

} // namespace sls
} // namespace aliyun
