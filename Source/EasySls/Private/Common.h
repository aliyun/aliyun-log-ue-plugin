#pragma once

#include "CoreMinimal.h"
#include "SlsConfig.h"
#include "TimeCorrector.h"

namespace aliyun
{
namespace sls
{

/**
 * @brief 内存控制器
 *
 * 用于追踪和限制缓冲区内存使用
 */
class FMemController
{
public:
    explicit FMemController(int64 InMaxBytes) : MaxBytes(InMaxBytes), CurrentBytes(0) {}

    /**
     * @brief 尝试申请内存
     * @param Bytes 申请的字节数
     * @return true 成功，false 超出限制
     */
    bool TryAcquire(int64 Bytes)
    {
        if (Bytes <= 0)
        {
            return true;
        }

        int64 Current = CurrentBytes.Load();
        while (true)
        {
            if (Current + Bytes > MaxBytes)
            {
                return false;
            }

            const int64 Desired = Current + Bytes;
            if (CurrentBytes.CompareExchange(Current, Desired))
            {
                return true;
            }
        }
    }

    /**
     * @brief 申请内存（不检查限制）
     */
    void Acquire(int64 Bytes)
    {
        if (Bytes > 0)
        {
            CurrentBytes += Bytes;
        }
    }

    /**
     * @brief 释放内存
     */
    void Release(int64 Bytes)
    {
        if (Bytes <= 0)
        {
            return;
        }

        int64 Current = CurrentBytes.Load();
        while (true)
        {
            const int64 Desired = FMath::Max<int64>(0, Current - Bytes);
            if (CurrentBytes.CompareExchange(Current, Desired))
            {
                return;
            }
        }
    }

    /**
     * @brief 获取当前使用量
     */
    int64 GetCurrentBytes() const
    {
        return CurrentBytes.Load();
    }

    /**
     * @brief 获取最大限制
     */
    int64 GetMaxBytes() const
    {
        return MaxBytes;
    }

    /**
     * @brief 是否已满
     */
    bool IsFull() const
    {
        return CurrentBytes.Load() >= MaxBytes;
    }

private:
    int64 MaxBytes;
    TAtomic<int64> CurrentBytes;
};

// HTTP Header 常量（声明）
namespace SlsHeaders
{
extern const FString ContentTypeProtobuf;
extern const FString XLogApiVersion;
extern const FString XLogApiVersion060;
extern const FString XLogCompressType;
extern const FString XLogCompressTypeLz4;
extern const FString XAcsSecurityToken;
extern const FString XLogSignatureMethod;
extern const FString XLogSignatureMethodHmacSha1;
extern const FString Date;
extern const FString ContentMd5;
extern const FString ContentLength;
extern const FString XLogBodyRawSize;
extern const FString Host;
extern const FString ContentType;
extern const FString Authorization;
} // namespace SlsHeaders

/**
 * @brief 压缩后的日志缓冲区
 */
struct FCompressedLogBuffer
{
    TArray<uint8> Data;   // 压缩后的数据
    int64 RawSize = 0;    // 原始长度（序列化后、压缩前）
    FString CompressType; // 压缩类型
    int32 LogCount = 0;   // 日志条数

    int64 CompressedLength() const
    {
        return Data.Num();
    }
};

/**
 * @brief HTTP 请求上下文
 */
struct FLogRequestContext
{
    FString HttpMethod;
    FString Uri;
    FString UriWithQuery;
    FString Host; // host with project prefix, without schema
    bool bUseHttps = false;
};

/**
 * @brief 安全凭证
 */
struct FSlsCredentials
{
    FString AccessKeyId;
    FString AccessKeySecret;
    FString SecurityToken;
};

struct FInnerProducerConfig;
using FInnerProducerConfigPtr = TSharedPtr<FInnerProducerConfig>;

/**
 * @brief 内部 Producer 配置
 *
 * 包含原始配置、解析后的请求上下文和内存控制器
 */
struct FInnerProducerConfig
{
    // 原始配置（拷贝）
    FSlsProducerConfig Config;

    // 解析后的请求上下文
    FLogRequestContext LogRequestContext;

    // 内存控制器
    TSharedPtr<FMemController> MemController;

    // 时间校正器
    TSharedPtr<FTimeCorrector> TimeCorrector;

    /**
     * @brief 从 FSlsProducerConfig 创建 FInnerProducerConfig
     * @param InConfig 原始配置
     * @return 如果配置无效返回 nullptr
     */
    static FInnerProducerConfigPtr Create(const FSlsProducerConfig& InConfig);

    /**
     * @brief 获取安全凭证
     */
    FSlsCredentials GetCredentials() const
    {
        FSlsCredentials Credentials;
        Credentials.AccessKeyId = Config.AccessKeyId;
        Credentials.AccessKeySecret = Config.AccessKeySecret;
        Credentials.SecurityToken = Config.SecurityToken;
        return Credentials;
    }

    FInnerProducerConfig(const FSlsProducerConfig& InConfig, const FLogRequestContext& InContext)
        : Config(InConfig),
          LogRequestContext(InContext),
          MemController(MakeShared<FMemController>(InConfig.MaxBufferBytes)),
          TimeCorrector(MakeShared<FTimeCorrector>(InConfig.bEnableTimeCorrection))
    {
    }
};

} // namespace sls
} // namespace aliyun
