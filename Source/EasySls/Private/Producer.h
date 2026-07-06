#pragma once

#include "CoreMinimal.h"
#include "LogItem.h"
#include "SlsConfig.h"

namespace aliyun
{
namespace sls
{

// 前向声明
class FFlusher;
class FSender;
class FSendTaskQueue;
class FSlsHttpClient;
class FPersistentManager;
class FRecoveryWorker;
struct FInnerProducerConfig;
struct FPersistenceConfig;

/**
 * @brief SLS 日志 Producer
 *
 * 用于高效地将日志发送到阿里云日志服务(SLS)。
 *
 * 架构：
 * - GameThread: AddLog() 入队（无阻塞）
 * - Flusher Thread (CPU密集): 聚合、序列化、压缩
 * - Sender Thread (IO密集): HTTP 发送、重试
 *
 * 使用示例：
 * @code
 * FSlsProducerConfig Config;
 * Config.Endpoint = TEXT("cn-hangzhou.log.aliyuncs.com");
 * Config.Project = TEXT("my-project");
 * Config.Logstore = TEXT("my-logstore");
 * Config.AccessKeyId = TEXT("access-key-id");
 * Config.AccessKeySecret = TEXT("access-key-secret");
 *
 * FSlsProducer Producer(Config);
 * Producer.Init();
 *
 * FLogItem Item;
 * Item.AddContent(TEXT("level"), TEXT("INFO"));
 * Item.AddContent(TEXT("message"), TEXT("Hello, SLS!"));
 * Producer.AddLog(Item);
 * @endcode
 */
class FSlsProducer
{
public:
    /**
     * @brief 构造函数
     * @param InConfig 配置
     * @param InCallback 用户发送结果回调（可选）
     */
    FSlsProducer(const FSlsProducerConfig& InConfig, FSlsSendCallback InCallback = nullptr);

    /**
     * @brief 析构函数
     */
    ~FSlsProducer();

    // 禁止拷贝和移动
    FSlsProducer(const FSlsProducer&) = delete;
    FSlsProducer& operator=(const FSlsProducer&) = delete;
    FSlsProducer(FSlsProducer&&) = delete;
    FSlsProducer& operator=(FSlsProducer&&) = delete;

    /**
     * @brief 初始化（必须先调用，之后才能使用其他方法）
     * @return true 成功，false 配置无效
     */
    bool Init();

    /**
     * @brief 添加日志
     * @param Item 日志项
     * @param bFlush 是否立即刷新
     * @return 返回 ESlsAddLogResult
     */
    ESlsAddLogResult AddLog(const FLogItem& Item, bool bFlush = false);
    ESlsAddLogResult AddLog(FLogItem&& Item, bool bFlush = false);

    /**
     * @brief 获取当前内存使用量（用于诊断和测试）
     * @return 当前内存使用字节数，-1 表示未初始化
     */
    int64 GetMemoryUsage() const;

private:
    FSlsProducerConfig Config;
    FSlsSendCallback UserCallback;

    TSharedPtr<FInnerProducerConfig> InnerConfig; // 保存用于测试访问
    TSharedPtr<FSlsHttpClient> HttpClient;
    TSharedPtr<FSendTaskQueue> TaskQueue;
    TSharedPtr<FFlusher> Flusher;
    TSharedPtr<FSender> Sender;

    // 持久化相关
    TSharedPtr<FPersistentManager> PersistentManager;
    TSharedPtr<FRecoveryWorker> RecoveryWorker;
};

} // namespace sls
} // namespace aliyun
