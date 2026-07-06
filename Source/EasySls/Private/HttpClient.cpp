#include "HttpClient.h"

namespace aliyun
{
namespace sls
{

FSlsHttpClient::FSlsHttpClient() = default;

FSlsHttpClient::~FSlsHttpClient()
{
    Stop();
}

void FSlsHttpClient::PostAsync(const FSlsHttpRequest& Request, FSlsHttpCallback Callback)
{
    if (!bRunning.Load())
    {
        if (Callback)
        {
            Callback(FSlsHttpResponse{0, TEXT(""), TEXT("ClientStopped"), TEXT("HTTP client is not running")});
        }
        return;
    }

    // 创建 HTTP 请求
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();

    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetURL(Request.Url);

    // 设置请求头
    for (const auto& Header : Request.Headers)
    {
        HttpRequest->SetHeader(Header.Key, Header.Value);
    }

    // 设置请求体
    HttpRequest->SetContent(Request.Body);

    // 设置超时
    HttpRequest->SetTimeout(Request.TimeoutSec);

    // ⭐ 关键：回调在 HTTP 线程执行，不切换到 GameThread
    HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

    // 绑定回调
    HttpRequest->OnProcessRequestComplete().BindLambda(
        [Callback = MoveTemp(Callback)](FHttpRequestPtr HttpRequestPtr,
                                        FHttpResponsePtr HttpResponse,
                                        bool bConnectedSuccessfully)
        {
            if (!Callback)
            {
                return;
            }

            if (!bConnectedSuccessfully || !HttpResponse.IsValid())
            {
                Callback(FSlsHttpResponse{0, TEXT(""), TEXT("NetworkError"), TEXT("Failed to connect to server")});
                return;
            }

            int32 StatusCode = HttpResponse->GetResponseCode();
            FString ResponseContent = HttpResponse->GetContentAsString();

            // 解析响应头获取 RequestId
            FString RequestId = HttpResponse->GetHeader(TEXT("x-log-requestid"));

            // 解析响应头获取服务端时间 (x-log-time)
            int64 ServerTime = 0;
            FString ServerTimeStr = HttpResponse->GetHeader(TEXT("x-log-time"));
            if (!ServerTimeStr.IsEmpty())
            {
                ServerTime = FCString::Atoi64(*ServerTimeStr);
            }

            // 解析错误信息
            FString ErrorCode;
            FString ErrorMessage;

            if (StatusCode < 200 || StatusCode >= 300)
            {
                ErrorMessage = ResponseContent;

                // 尝试提取 errorCode
                int32 CodeStart = ResponseContent.Find(TEXT("\"errorCode\""));
                if (CodeStart != INDEX_NONE)
                {
                    int32 ValueStart = ResponseContent.Find(TEXT(":"),
                                                            ESearchCase::IgnoreCase,
                                                            ESearchDir::FromStart,
                                                            CodeStart);
                    int32 QuoteStart = ResponseContent.Find(TEXT("\""),
                                                            ESearchCase::IgnoreCase,
                                                            ESearchDir::FromStart,
                                                            ValueStart);
                    int32 QuoteEnd = ResponseContent.Find(TEXT("\""),
                                                          ESearchCase::IgnoreCase,
                                                          ESearchDir::FromStart,
                                                          QuoteStart + 1);
                    if (QuoteStart != INDEX_NONE && QuoteEnd != INDEX_NONE)
                    {
                        ErrorCode = ResponseContent.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
                    }
                }
            }

            Callback(FSlsHttpResponse{StatusCode, RequestId, ErrorCode, ErrorMessage, ServerTime});
        });

    // 发送请求（非阻塞）
    HttpRequest->ProcessRequest();
}

void FSlsHttpClient::Start()
{
    bRunning.Store(true);
}

void FSlsHttpClient::Stop()
{
    bRunning.Store(false);
}

} // namespace sls
} // namespace aliyun
