#pragma once

#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Common.h"
#include "CoreMinimal.h"
#include "PersistentFile.h"
#include "PersistentManager.h"
#include "SlsConfig.h"

#ifndef UNIT_TEST_FRIEND
#define UNIT_TEST_FRIEND
#endif

#if WITH_DEV_AUTOMATION_TESTS
class FPersistenceAndRecoveryTest;
class FRotationRecoveryTest;
#endif

namespace aliyun
{
namespace sls
{

class FSlsHttpClient;

/**
 * FRecoveryWorker - 轮询 recover/ 目录，处理待恢复文件
 *
 * 职责：
 * - 轮询 RecoverDir_ 目录
 * - 清理过期文件（超过 MaxPersistenceTimeSec）
 * - 按文件名排序，取最早的文件加载（ScanHeaders 预记录所有 Block 偏移）
 * - 按 Block 索引遍历，跳过已 finalized 的 Block，读取 Payload 并发送
 * - 全部处理完成或 CRC 校验失败时删除文件
 * - 无文件时 sleep
 */
class FRecoveryWorker : public FRunnable
{
#if WITH_DEV_AUTOMATION_TESTS
    friend class ::FPersistenceAndRecoveryTest;
    friend class ::FRotationRecoveryTest;
#endif
    UNIT_TEST_FRIEND;

public:
    FRecoveryWorker(FInnerProducerConfigPtr InConfig,
                    TSharedPtr<FSlsHttpClient> InHttpClient,
                    const FPersistenceConfig& InPersistConfig);

    ~FRecoveryWorker();

    FRecoveryWorker(const FRecoveryWorker&) = delete;
    FRecoveryWorker& operator=(const FRecoveryWorker&) = delete;

    void Start();
    virtual void Stop() override;

    virtual bool Init() override
    {
        return true;
    }
    virtual uint32 Run() override;

private:
    /**
     * 扫描 recover/ 目录，清理过期文件，加载并返回最早的可用文件
     * 返回的文件已通过 LoadAndValidate（Header 已扫描，偏移已记录）
     */
    TSharedPtr<FPersistentFile> LoadEarliestRecoverFile();

    void ProcessFile(TSharedPtr<FPersistentFile> File);
    FSlsSendResult SyncSendWithRetry(const TArray<uint8>& BlockData, const FBlockHeader& Header);
    FSlsSendResult SyncSendHttp(const TArray<uint8>& BlockData, const FBlockHeader& Header);

    /** 从文件名解析创建时间戳（秒） */
    static int64 ParseCreateTimeFromBaseName(const FString& BaseName);

    FInnerProducerConfigPtr Config_;
    TSharedPtr<FSlsHttpClient> HttpClient_;
    FPersistenceConfig PersistConfig_;
    FString RecoverDir_;

    FEventRef ShutdownEvent_{EEventMode::ManualReset};
    TAtomic<bool> bShutdown_{false};
    TAtomic<bool> bIdle_{false};
    FRunnableThread* WorkerThread_ = nullptr;

    static constexpr int32 PollIntervalMs_ = 5000;
};

} // namespace sls
} // namespace aliyun
