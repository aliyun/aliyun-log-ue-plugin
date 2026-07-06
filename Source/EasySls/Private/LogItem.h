#pragma once

#include "CoreMinimal.h"
#include "Utils.h"

namespace aliyun
{
namespace sls
{

/**
 * @brief 日志项，包含多个 key-value 对
 *
 * 使用示例：
 * @code
 * FLogItem Item;
 * Item.SetTime(FUtils::GetCurrentTimeSec());
 * Item.AddContent(TEXT("level"), TEXT("INFO"));
 * Item.AddContent(TEXT("message"), TEXT("Hello, world!"));
 * @endcode
 */
class FLogItem
{
public:
    using FKeyValue = TPair<FString, FString>;
    using FContents = TArray<FKeyValue>;

    /**
     * @brief 默认构造，时间戳设为当前时间
     */
    FLogItem() : Timestamp(FUtils::GetCurrentTimeSec()) {}

    /**
     * @brief 使用指定时间戳构造
     */
    explicit FLogItem(uint32 InTimestamp) : Timestamp(InTimestamp) {}

    /**
     * @brief 设置时间戳（Unix 秒）
     */
    void SetTime(uint32 InTimestamp)
    {
        Timestamp = InTimestamp;
    }

    /**
     * @brief 获取时间戳
     */
    uint32 GetTime() const
    {
        return Timestamp;
    }

    /**
     * @brief 添加一对 key-value
     */
    void AddContent(const FString& Key, const FString& Value)
    {
        Contents.Emplace(Key, Value);
    }

    /**
     * @brief 添加一对 key-value（移动语义）
     */
    void AddContent(FString&& Key, FString&& Value)
    {
        Contents.Emplace(MoveTemp(Key), MoveTemp(Value));
    }

    /**
     * @brief 获取所有内容
     */
    const FContents& GetContents() const
    {
        return Contents;
    }

    /**
     * @brief 获取内容数量
     */
    int32 Num() const
    {
        return Contents.Num();
    }

    /**
     * @brief 是否为空
     */
    bool IsEmpty() const
    {
        return Contents.Num() == 0;
    }

    /**
     * @brief 清空内容
     */
    void Clear()
    {
        Contents.Empty();
        Timestamp = 0;
    }

    /**
     * @brief 计算估算的字节大小
     */
    int64 EstimateSize() const
    {
        int64 Size = sizeof(Timestamp);
        for (const auto& Kv : Contents)
        {
            Size += Kv.Key.Len() + Kv.Value.Len() + 8; // 额外的 protobuf 开销
        }
        return Size;
    }

private:
    uint32 Timestamp;
    FContents Contents;
};

} // namespace sls
} // namespace aliyun
