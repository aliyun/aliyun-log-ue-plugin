#include "Signature.h"

#include "ThirdParty.h"

namespace aliyun
{
namespace sls
{

FString FSignature::BuildStringToSign(const FString& HttpMethod,
                                      const FString& UriWithQuery,
                                      const FString& ContentMd5,
                                      const FString& Date,
                                      const FString& CompressType,
                                      const FString& BodyRawSize,
                                      const FString& SecurityToken)
{
    FString Result;

    // HTTP Method
    Result += HttpMethod + TEXT("\n");

    // Content-MD5
    Result += ContentMd5 + TEXT("\n");

    // Content-Type
    Result += SlsHeaders::ContentTypeProtobuf + TEXT("\n");

    // Date
    Result += Date + TEXT("\n");

    // signed headers (按字母顺序)
    if (!SecurityToken.IsEmpty())
    {
        Result += SlsHeaders::XAcsSecurityToken + TEXT(":") + SecurityToken + TEXT("\n");
    }
    Result += SlsHeaders::XLogApiVersion + TEXT(":") + SlsHeaders::XLogApiVersion060 + TEXT("\n");
    Result += SlsHeaders::XLogBodyRawSize + TEXT(":") + BodyRawSize + TEXT("\n");
    if (!CompressType.IsEmpty())
    {
        Result += SlsHeaders::XLogCompressType + TEXT(":") + SlsHeaders::XLogCompressTypeLz4 + TEXT("\n");
    }
    Result += SlsHeaders::XLogSignatureMethod + TEXT(":") + SlsHeaders::XLogSignatureMethodHmacSha1 + TEXT("\n");

    Result += UriWithQuery;
    return Result;
}

TMap<FString, FString> FSignature::GenerateSignature(const FCompressedLogBuffer& Data,
                                                     const FSlsCredentials& Credentials,
                                                     const FLogRequestContext& RequestContext,
                                                     const FTimeCorrector& TimeCorrector)
{
    // 计算 Content-MD5
    FString ContentMd5 = thirdparty::MD5Hash(Data.Data);

    // 获取日期（TimeCorrector 内部通过 bEnabled 控制是否校正）
    FString Date = TimeCorrector.GetRfc1123Date();

    // 原始大小字符串
    FString BodyRawSizeStr = FString::Printf(TEXT("%lld"), Data.RawSize);

    const FString& HttpMethod = RequestContext.HttpMethod;
    const FString& UriWithQuery = RequestContext.UriWithQuery;
    const FString& Host = RequestContext.Host;

    // 构建 HTTP 部头
    TMap<FString, FString> Headers;
    Headers.Add(SlsHeaders::XLogBodyRawSize, BodyRawSizeStr);
    if (!Data.CompressType.IsEmpty())
    {
        Headers.Add(SlsHeaders::XLogCompressType, Data.CompressType);
    }
    Headers.Add(SlsHeaders::XLogSignatureMethod, SlsHeaders::XLogSignatureMethodHmacSha1);
    Headers.Add(SlsHeaders::Date, Date);
    Headers.Add(SlsHeaders::ContentMd5, ContentMd5);
    Headers.Add(SlsHeaders::ContentType, SlsHeaders::ContentTypeProtobuf);
    Headers.Add(SlsHeaders::Host, Host);

    if (!Credentials.SecurityToken.IsEmpty())
    {
        Headers.Add(SlsHeaders::XAcsSecurityToken, Credentials.SecurityToken);
    }
    Headers.Add(SlsHeaders::XLogApiVersion, SlsHeaders::XLogApiVersion060);

    // 构建待签名字符串
    FString StringToSign = BuildStringToSign(HttpMethod,
                                             UriWithQuery,
                                             ContentMd5,
                                             Date,
                                             Data.CompressType,
                                             BodyRawSizeStr,
                                             Credentials.SecurityToken);

    // 计算 HMAC-SHA1 签名并 Base64 编码
    TArray<uint8> SignatureBytes = thirdparty::HmacSha1(Credentials.AccessKeySecret, StringToSign);
    FString Signature = thirdparty::Base64Encode(SignatureBytes);

    // 构建 Authorization 头
    FString Authorization = FString::Printf(TEXT("LOG %s:%s"), *Credentials.AccessKeyId, *Signature);
    Headers.Add(SlsHeaders::Authorization, Authorization);

    return Headers;
}
} // namespace sls
} // namespace aliyun
