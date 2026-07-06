#pragma once

#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Common.h"
#include "CoreMinimal.h"
#include "HttpClient.h"
#include "SlsConfig.h"

namespace aliyun
{
namespace sls
{

// 前向声明
class FSendTaskQueue;
struct FSendTask;

/**
 * @brief 发送完成回调（内部使用）
 * @param Task 发送的任务
 * @param Result 发送结果
 *
 * 此回调由 Producer 包装，负责：
 * - 释放内存计数器
 * - 调用用户回调（如果有）
 */
using FSendCallback = TFunction<void(TSharedPtr<FSendTask> Task, const FSlsSendResult& Result)>;

/**
 * @brief 发送器
 *
 * 负责：
 * - 从 TaskQueue 取任务
 * - 异步发送 HTTP 请求
 * - 失败时将任务放回 TaskQueue（延迟重试）
 */
class FSender : public FRunnable
{
public:
    /**
     * @brief 构造函数
     * @param InHttpClient HTTP 客户端
     * @param InTaskQueue 发送任务队列
     * @param InSendCallback 发送完成回调
     */
    FSender(TSharedPtr<FSlsHttpClient> InHttpClient,
            TSharedPtr<FSendTaskQueue> InTaskQueue,
            FSendCallback InSendCallback);

    ~FSender();

    // 禁止拷贝
    FSender(const FSender&) = delete;
    FSender& operator=(const FSender&) = delete;

    /**
     * @brief 启动发送器
     */
    void Start();

    /**
     * @brief 停止发送器（也是 FRunnable::Stop 的实现）
     */
    virtual void Stop() override;

    // FRunnable interface
    virtual bool Init() override
    {
        return true;
    }
    virtual uint32 Run() override;

    // ==========================================
    // 公共静态方法（可被 RecoveryWorker 复用）
    // ==========================================

    /**
     * @brief 构建 HTTP 请求
     * @param Config 配置
     * @param Buffer 压缩后的日志数据
     * @return HTTP 请求
     */
    static FSlsHttpRequest BuildRequest(FInnerProducerConfigPtr Config, const FCompressedLogBuffer& Buffer);

    /** 判断错误码是否应该重试 */
    static bool ShouldRetry(int32 ResultCode);

    /** 计算重试等待时间（带抖动的指数退避） */
    static int32 CalcRetryWaitMs(int32 RetryCount);

    /** 最大重试次数 */
    static constexpr int32 MAX_RETRIES = 3;

    /** 解析 HTTP 状态码为内部结果码 */
    static int32 ParseStatusCode(int32 StatusCode, const FString& ErrorCode);

private:
    void FinalizeTask(TSharedPtr<FSendTask> Task, const FSlsSendResult& Result) const;

    void DoSend(TSharedPtr<FSendTask> Task);

    struct FAsyncState
    {
        TAtomic<int32> InFlightRequestCount{0};
        FEventRef InFlightDoneEvent{EEventMode::ManualReset};
    };

    static void HandleResponse(TSharedPtr<FSendTask> Task,
                               const FSlsHttpResponse& Response,
                               TSharedPtr<FSendTaskQueue> TaskQueue,
                               FSendCallback SendCallback,
                               TSharedPtr<FAsyncState> AsyncState);

    TSharedPtr<FSlsHttpClient> HttpClient;
    TSharedPtr<FSendTaskQueue> TaskQueue;
    TSharedPtr<FAsyncState> AsyncState;
    FSendCallback SendCallback;

    TAtomic<bool> bShutdown{false};
    FRunnableThread* WorkerThread = nullptr;
};

} // namespace sls
} // namespace aliyun
