#pragma once

#include "Common.h"
#include "CoreMinimal.h"

namespace aliyun
{
namespace sls
{

/**
 * @brief 签名生成器
 *
 * 用于生成 SLS API 请求的签名。
 */
class FSignature
{
public:
    /**
     * @brief 生成 SLS API 签名
     * @param Data 压缩后的日志数据
     * @param Credentials 安全凭证
     * @param RequestContext HTTP 请求上下文
     * @param TimeCorrector 时间校正器（用于修正客户端与服务端的时间差，通过 bEnabled 控制是否启用）
     * @return HTTP 头部键值对
     */
    static TMap<FString, FString> GenerateSignature(const FCompressedLogBuffer& Data,
                                                    const FSlsCredentials& Credentials,
                                                    const FLogRequestContext& RequestContext,
                                                    const FTimeCorrector& TimeCorrector);

private:
    FSignature() = delete;

    /**
     * @brief 构建待签名字符串
     */
    static FString BuildStringToSign(const FString& HttpMethod,
                                     const FString& UriWithQuery,
                                     const FString& ContentMd5,
                                     const FString& Date,
                                     const FString& CompressType,
                                     const FString& BodyRawSize,
                                     const FString& SecurityToken);
};

} // namespace sls
} // namespace aliyun
