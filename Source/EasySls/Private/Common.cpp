#include "Common.h"
#include "InnerLog.h"

namespace aliyun
{
namespace sls
{

// HTTP Header 常量定义
namespace SlsHeaders
{
const FString ContentTypeProtobuf = TEXT("application/x-protobuf");
const FString XLogApiVersion = TEXT("x-log-apiversion");
const FString XLogApiVersion060 = TEXT("0.6.0");
const FString XLogCompressType = TEXT("x-log-compresstype");
const FString XLogCompressTypeLz4 = TEXT("lz4");
const FString XAcsSecurityToken = TEXT("x-acs-security-token");
const FString XLogSignatureMethod = TEXT("x-log-signaturemethod");
const FString XLogSignatureMethodHmacSha1 = TEXT("hmac-sha1");
const FString Date = TEXT("Date");
const FString ContentMd5 = TEXT("Content-MD5");
const FString ContentLength = TEXT("Content-Length");
const FString XLogBodyRawSize = TEXT("x-log-bodyrawsize");
const FString Host = TEXT("Host");
const FString ContentType = TEXT("Content-Type");
const FString Authorization = TEXT("Authorization");
} // namespace SlsHeaders

namespace
{

// 解析 endpoint，提取 host 并检测是否使用 HTTPS
FString ParseEndpoint(const FString& Endpoint, bool& bOutUseHttps)
{
    FString LowerEndpoint = Endpoint.ToLower();

    if (LowerEndpoint.StartsWith(TEXT("https://")))
    {
        bOutUseHttps = true;
        return Endpoint.Mid(8);
    }

    if (LowerEndpoint.StartsWith(TEXT("http://")))
    {
        bOutUseHttps = false;
        return Endpoint.Mid(7);
    }

    bOutUseHttps = false;
    return Endpoint;
}

} // anonymous namespace

FInnerProducerConfigPtr FInnerProducerConfig::Create(const FSlsProducerConfig& Cfg)
{
    // 验证必填字段
    if (Cfg.Endpoint.IsEmpty())
    {
        SLS_LOG_ERROR("SlsProducerConfig validation failed: Endpoint is empty");
        return nullptr;
    }
    if (Cfg.Project.IsEmpty())
    {
        SLS_LOG_ERROR("SlsProducerConfig validation failed: Project is empty");
        return nullptr;
    }
    if (Cfg.Logstore.IsEmpty())
    {
        SLS_LOG_ERROR("SlsProducerConfig validation failed: Logstore is empty");
        return nullptr;
    }
    if (Cfg.AccessKeyId.IsEmpty())
    {
        SLS_LOG_ERROR("SlsProducerConfig validation failed: AccessKeyId is empty");
        return nullptr;
    }
    if (Cfg.AccessKeySecret.IsEmpty())
    {
        SLS_LOG_ERROR("SlsProducerConfig validation failed: AccessKeySecret is empty");
        return nullptr;
    }

    // 解析 endpoint
    bool bUseHttps = Cfg.bUsingHttps;
    FString Host = ParseEndpoint(Cfg.Endpoint, bUseHttps);
    if (Host.IsEmpty())
    {
        SLS_LOG_ERROR("SlsProducerConfig validation failed: Failed to parse Endpoint '%s'", *Cfg.Endpoint);
        return nullptr;
    }

    // 构建请求上下文
    FLogRequestContext Ctx;
    Ctx.HttpMethod = TEXT("POST");
    Ctx.Uri = FString::Printf(TEXT("/logstores/%s/shards/lb"), *Cfg.Logstore);
    Ctx.Host = FString::Printf(TEXT("%s.%s"), *Cfg.Project, *Host);
    Ctx.bUseHttps = bUseHttps;
    Ctx.UriWithQuery = Ctx.Uri;

    return MakeShared<FInnerProducerConfig>(Cfg, Ctx);
}

} // namespace sls
} // namespace aliyun
