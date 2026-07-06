#pragma once

#include "CoreMinimal.h"
#include "LogItem.h"

namespace aliyun
{
namespace sls
{

/**
 * @brief 日志组构建器
 *
 * 用于将多条日志聚合成一个日志组，并序列化为 protobuf 格式。
 */
class FLogGroupBuilder
{
public:
    FLogGroupBuilder();
    ~FLogGroupBuilder() = default;

    // 禁止拷贝
    FLogGroupBuilder(const FLogGroupBuilder&) = delete;
    FLogGroupBuilder& operator=(const FLogGroupBuilder&) = delete;

    // 允许移动
    FLogGroupBuilder(FLogGroupBuilder&&) = default;
    FLogGroupBuilder& operator=(FLogGroupBuilder&&) = default;

    /**
     * @brief 添加一条日志
     */
    void AddLog(const FLogItem& Item);

    /**
     * @brief 添加一条日志（移动语义）
     */
    void AddLog(FLogItem&& Item);

    /**
     * @brief 设置日志组来源
     */
    void SetSource(const FString& InSource);

    /**
     * @brief 设置日志组主题
     */
    void SetTopic(const FString& InTopic);

    /**
     * @brief 添加标签
     */
    void AddTag(const FString& Key, const FString& Value);

    /**
     * @brief 设置 Pack ID
     */
    void SetPackId(const FString& InPackId);

    /**
     * @brief 获取日志数量
     */
    int32 LogCount() const
    {
        return Logs.Num();
    }

    /**
     * @brief 获取估算的数据大小
     */
    int64 EstimatedSize() const
    {
        return EstimatedSizeBytes;
    }

    /**
     * @brief 是否为空
     */
    bool IsEmpty() const
    {
        return Logs.Num() == 0;
    }

    /**
     * @brief 序列化为 protobuf 格式
     * @return 序列化后的数据
     */
    TArray<uint8> Serialize() const;

    /**
     * @brief 获取构建器创建时间
     */
    uint32 GetBuilderTime() const
    {
        return BuilderTime;
    }

private:
    /**
     * @brief 写入 protobuf varint
     */
    static void WriteVarint(TArray<uint8>& Output, uint64 Value);

    /**
     * @brief 写入 protobuf 字符串字段（UTF-8）
     */
    static void WriteStringField(TArray<uint8>& Output, int32 FieldNumber, const FString& Value);

    /**
     * @brief 写入 protobuf 嵌套消息
     */
    static void WriteNestedMessage(TArray<uint8>& Output, int32 FieldNumber, const TArray<uint8>& Message);

    /**
     * @brief 序列化单条日志
     */
    TArray<uint8> SerializeLog(const FLogItem& Item) const;

    TArray<FLogItem> Logs;
    FString Source;
    FString Topic;
    TArray<TPair<FString, FString>> Tags;
    FString PackId;
    int64 EstimatedSizeBytes = 0;
    uint32 BuilderTime = 0;
};

} // namespace sls
} // namespace aliyun
