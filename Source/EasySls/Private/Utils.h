#pragma once

#include "CoreMinimal.h"

namespace aliyun
{
namespace sls
{

/**
 * @brief 工具函数集合
 */
class FUtils
{
public:
    /**
     * @brief 获取当前 Unix 时间戳（秒）
     */
    static uint32 GetCurrentTimeSec();

    /**
     * @brief 获取当前时间戳（毫秒）
     */
    static int64 GetCurrentTimeMs();

    /**
     * @brief 获取当前时间戳（纳秒）
     */
    static int64 GetCurrentTimeNs();

    /**
     * @brief 生成 Pack ID 前缀
     * @param ConfigName 配置名称
     * @param Source 来源
     * @return Pack ID 前缀（16字符十六进制）
     */
    static FString GeneratePackIdPrefix(const FString& ConfigName, const FString& Source);

    /**
     * @brief 睡眠指定毫秒
     */
    static void SleepMs(int32 Ms);

    /**
     * @brief 格式化 Pack ID
     * @param Prefix 前缀
     * @param Index 序号
     * @return 完整的 Pack ID
     */
    static FString FormatPackId(const FString& Prefix, uint32 Index);

private:
    FUtils() = delete;
};

} // namespace sls
} // namespace aliyun
