#pragma once

#include "UObject/ObjectMacros.h"
#include "CoreMinimal.h"

#include "SlsConfig.generated.h"

/**
 * @brief 压缩类型
 */
UENUM(BlueprintType)
enum class ESlsCompressType : uint8
{
    None UMETA(DisplayName = "none"),
    Lz4 UMETA(DisplayName = "lz4")
};

/**
 * @brief AddLog 对外返回码
 */
UENUM(BlueprintType)
enum class ESlsAddLogResult : uint8
{
    Ok = 0 UMETA(DisplayName = "Ok"),
    MemoryReachLimit = 12 UMETA(DisplayName = "MemoryReachLimit"),
    ProducerNotExist = 28 UMETA(DisplayName = "ProducerNotExist")
};

/**
 * @brief 日志标签
 */
USTRUCT(BlueprintType)
struct EASYSLS_API FSlsTag
{

    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Tag")
    FString Key;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Tag")
    FString Value;

    FSlsTag() = default;
    FSlsTag(const FString& InKey, const FString& InValue) : Key(InKey), Value(InValue) {}
};

/**
 * @brief SLS Producer 配置
 *
 * 使用示例：
 * @code
 * FSlsProducerConfig Config;
 * Config.Endpoint = "cn-hangzhou.log.aliyuncs.com";
 * Config.Project = "my-project";
 * Config.Logstore = "my-logstore";
 * Config.AccessKeyId = "xxx";
 * Config.AccessKeySecret = "xxx";
 * @endcode
 */
USTRUCT(BlueprintType)
struct EASYSLS_API FSlsProducerConfig
{

    GENERATED_BODY()

    // ============ 默认值常量 ============
    static constexpr int32 DEFAULT_PACKAGE_TIMEOUT_MS = 3000;
    static constexpr int32 DEFAULT_LOG_COUNT_PER_PACKAGE = 4096;
    static constexpr int32 DEFAULT_LOG_BYTES_PER_PACKAGE = 3 * 1024 * 1024;
    static constexpr int64 DEFAULT_MAX_BUFFER_BYTES = 64 * 1024 * 1024;
    static constexpr int32 DEFAULT_FLUSH_INTERVAL_MS = 1000;
    static constexpr int32 DEFAULT_CONNECT_TIMEOUT_SEC = 10;
    static constexpr int32 DEFAULT_SEND_TIMEOUT_SEC = 15;
    static constexpr int32 DEFAULT_DESTROY_FLUSHER_WAIT_SEC = 1;
    static constexpr int32 DEFAULT_DESTROY_SENDER_WAIT_SEC = 1;
    static constexpr int32 DEFAULT_MAX_LOG_DELAY_TIME = 7 * 24 * 3600;

    // ============ 基础配置 ============
    /** SLS 服务端点，例如 cn-hangzhou.log.aliyuncs.com */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Basic",
              meta = (ToolTip = "SLS 服务端点，例如 cn-hangzhou.log.aliyuncs.com"))
    FString Endpoint;

    /** SLS 项目名称 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Basic", meta = (ToolTip = "SLS 项目名称"))
    FString Project;

    /** SLS 日志库名称 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Basic", meta = (ToolTip = "SLS 日志库名称"))
    FString Logstore;

    /** 阿里云 AccessKey ID */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Credentials", meta = (ToolTip = "阿里云 AccessKey ID"))
    FString AccessKeyId;

    /** 阿里云 AccessKey Secret */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Credentials",
              meta = (ToolTip = "阿里云 AccessKey Secret"))
    FString AccessKeySecret;

    /** STS 安全令牌（可选，使用 STS 时填写） */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Credentials",
              meta = (ToolTip = "STS 安全令牌（可选，使用 STS 时填写）"))
    FString SecurityToken;

    /** 日志来源标识 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Basic", meta = (ToolTip = "日志来源标识"))
    FString Source;

    /** 日志主题 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Basic", meta = (ToolTip = "日志主题"))
    FString Topic;

    /** 日志标签列表 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Basic", meta = (ToolTip = "日志标签列表"))
    TArray<FSlsTag> Tags;

    // ============ 打包配置 ============
    /** 日志打包超时时间（毫秒），默认 3000 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Package",
              meta = (ToolTip = "日志打包超时时间（毫秒），默认 3000"))
    int32 PackageTimeoutMs = DEFAULT_PACKAGE_TIMEOUT_MS;

    /** 单个包的最大日志条数，默认 4096 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Package",
              meta = (ToolTip = "单个包的最大日志条数，默认 4096"))
    int32 LogCountPerPackage = DEFAULT_LOG_COUNT_PER_PACKAGE;

    /** 单个包的最大字节数，默认 3MB */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Package",
              meta = (ToolTip = "单个包的最大字节数，默认 3MB"))
    int32 LogBytesPerPackage = DEFAULT_LOG_BYTES_PER_PACKAGE;

    /** 最大缓冲区字节数，默认 64MB */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Package",
              meta = (ToolTip = "最大缓冲区字节数，默认 64MB"))
    int64 MaxBufferBytes = DEFAULT_MAX_BUFFER_BYTES;

    /** 刷新间隔（毫秒），默认 1000 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Package",
              meta = (ToolTip = "刷新间隔（毫秒），默认 1000"))
    int32 FlushIntervalMs = DEFAULT_FLUSH_INTERVAL_MS;

    // ============ 网络配置 ============
    /** 连接超时时间（秒），默认 10 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Network",
              meta = (ToolTip = "连接超时时间（秒），默认 10"))
    int32 ConnectTimeoutSec = DEFAULT_CONNECT_TIMEOUT_SEC;

    /** 发送超时时间（秒），默认 15 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Network",
              meta = (ToolTip = "发送超时时间（秒），默认 15"))
    int32 SendTimeoutSec = DEFAULT_SEND_TIMEOUT_SEC;

    /** 网络接口（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Network", meta = (ToolTip = "网络接口（可选）"))
    FString NetInterface;

    /** 是否使用 HTTPS，默认 true */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Network",
              meta = (ToolTip = "是否使用 HTTPS，默认 true"))
    bool bUsingHttps = true;

    // ============ 压缩配置 ============
    /** 压缩类型，默认 LZ4 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Compress", meta = (ToolTip = "压缩类型，默认 LZ4"))
    ESlsCompressType CompressType = ESlsCompressType::Lz4;

    // ============ 销毁配置 ============
    /** Flusher 销毁等待时间（秒），默认 1 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Destroy",
              meta = (ToolTip = "Flusher 销毁等待时间（秒），默认 1"))
    int32 DestroyFlusherWaitSec = DEFAULT_DESTROY_FLUSHER_WAIT_SEC;

    /** Sender 销毁等待时间（秒），默认 1 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Destroy",
              meta = (ToolTip = "Sender 销毁等待时间（秒），默认 1"))
    int32 DestroySenderWaitSec = DEFAULT_DESTROY_SENDER_WAIT_SEC;

    // ============ 时间配置 ============
    /** NTP 时间偏移（秒），默认 0 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Time", meta = (ToolTip = "NTP 时间偏移（秒），默认 0"))
    int32 NtpTimeOffset = 0;

    /** 最大日志延迟时间（秒），默认 7 天 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Time",
              meta = (ToolTip = "最大日志延迟时间（秒），默认 7 天"))
    int32 MaxLogDelayTime = DEFAULT_MAX_LOG_DELAY_TIME;

    /** 是否丢弃延迟日志，默认 true */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Time", meta = (ToolTip = "是否丢弃延迟日志，默认 true"))
    bool bDropDelayLog = true;

    /** 是否丢弃未授权日志，默认 false */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Time",
              meta = (ToolTip = "是否丢弃未授权日志，默认 false"))
    bool bDropUnauthorizedLog = false;

    /** 是否启用时间校正，默认 true。当客户端与服务端时间差超过 1 分钟时自动修正 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SLS|Time", meta = (ToolTip = "是否启用时间校正，默认 true"))
    bool bEnableTimeCorrection = true;

    // ============ 持久化配置 ============
    /** 是否启用持久化（断点续传），默认 false */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Persistence",
              meta = (ToolTip = "是否启用持久化（断点续传），默认 false"))
    bool bEnablePersistence = false;

    /** 持久化目录，为空时使用默认路径 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Persistence",
              meta = (ToolTip = "持久化目录，为空时使用默认路径"))
    FString PersistenceDir;

    /** 单个持久化文件最大大小（字节），默认 10MB */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Persistence",
              meta = (ToolTip = "单个持久化文件最大大小（字节），默认 10MB"))
    int64 MaxPersistentFileSize = 10 * 1024 * 1024;

    /** 最大持久化文件数量（软限制），默认 10 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Persistence",
              meta = (ToolTip = "最大持久化文件数量（软限制），默认 10"))
    int32 MaxPersistentFileCount = 10;

    /** 持久化文件最大保存时间（秒），超过此时间的文件在恢复时直接删除。默认 7 天 */
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "SLS|Persistence",
              meta = (ToolTip = "持久化文件最大保存时间（秒），默认 604800（7天）"))
    int64 MaxPersistenceTimeSec = 7 * 24 * 3600;
};

/**
 * @brief 日志发送结果
 */
struct EASYSLS_API FSlsSendResult
{
    int32 ResultCode = 0;
    FString ErrorMessage;
    FString RequestId;
    int32 LogCount = 0; // 本次发送的日志条数

    bool IsSuccess() const
    {
        return ResultCode == 0;
    }

    FSlsSendResult() = default;
    FSlsSendResult(int32 InResultCode, const FString& InErrorMessage, const FString& InRequestId, int32 InLogCount = 0)
        : ResultCode(InResultCode),
          ErrorMessage(InErrorMessage),
          RequestId(InRequestId),
          LogCount(InLogCount)
    {
    }
};

/**
 * @brief 用户发送完成回调
 */
using FSlsSendCallback = TFunction<void(const FSlsSendResult& Result)>;
