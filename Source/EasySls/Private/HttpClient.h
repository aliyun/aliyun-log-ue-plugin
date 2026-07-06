#pragma once

#include "CoreMinimal.h"
#include "Http.h"

namespace aliyun
{
namespace sls
{

/**
 * @brief HTTP 响应
 */
struct FSlsHttpResponse
{
    int32 StatusCode = 0;
    FString RequestId;
    FString ErrorCode;
    FString ErrorMessage;
    int64 ServerTime = 0; // 服务端时间 (Unix timestamp, 从 x-log-time 头获取), <= 0 为 invalid

    FSlsHttpResponse() = default;

    FSlsHttpResponse(int32 InStatusCode,
                     const FString& InRequestId,
                     const FString& InErrorCode,
                     const FString& InErrorMessage,
                     int64 InServerTime = 0)
        : StatusCode(InStatusCode),
          RequestId(InRequestId),
          ErrorCode(InErrorCode),
          ErrorMessage(InErrorMessage),
          ServerTime(InServerTime)
    {
    }

    static FSlsHttpResponse Success(const FString& InRequestId = TEXT(""))
    {
        return FSlsHttpResponse{200, InRequestId, TEXT(""), TEXT(""), 0};
    }

    bool IsSuccess() const
    {
        return StatusCode >= 200 && StatusCode < 300;
    }
};

/**
 * @brief HTTP 请求
 */
struct FSlsHttpRequest
{
    FString Url;
    TMap<FString, FString> Headers;
    TArray<uint8> Body;
    float TimeoutSec = 15.0f;
};

/**
 * @brief 异步 HTTP 回调
 */
using FSlsHttpCallback = TFunction<void(const FSlsHttpResponse& Response)>;

/**
 * @brief HTTP 客户端
 *
 * 使用 UE 的 FHttpModule 实现异步 HTTP 请求
 * 回调在 HTTP 线程执行，不占用 GameThread
 */
class FSlsHttpClient
{
public:
    FSlsHttpClient();
    virtual ~FSlsHttpClient();

    /**
     * @brief 异步发送 POST 请求
     * @param Request HTTP 请求
     * @param Callback 完成回调（在 HTTP 线程执行）
     */
    virtual void PostAsync(const FSlsHttpRequest& Request, FSlsHttpCallback Callback);

    /**
     * @brief 启动客户端
     */
    virtual void Start();

    /**
     * @brief 停止客户端
     */
    virtual void Stop();

    /**
     * @brief 是否正在运行
     */
    bool IsRunning() const
    {
        return bRunning.Load();
    }

private:
    TAtomic<bool> bRunning{false};
};

} // namespace sls
} // namespace aliyun
