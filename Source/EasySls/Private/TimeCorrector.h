#pragma once

#include "HAL/ThreadSafeCounter.h"
#include "Misc/DateTime.h"
#include "CoreMinimal.h"
#include "InnerLog.h"

#if WITH_DEV_AUTOMATION_TESTS
class FTimeCorrectorDisabledTest;
class FTimeCorrectorSmallDeltaTest;
class FTimeCorrectorLargeDeltaTest;
class FTimeCorrectorUpdateIntervalTest;
class FTimeCorrectorNegativeDeltaTest;
class FTimeCorrectorNeverUpdatedTest;
#endif

namespace aliyun
{
namespace sls
{

/**
 * @brief 时间校正器
 *
 * 用于校正客户端与服务端的时间差，确保签名时间戳有效。
 *
 * 特性：
 * - 线程安全：可被多线程并发调用
 * - 性能优化：OnServerResponseTime 调用间隔小于 5 秒时跳过更新
 * - 智能修正：时间差小于 1 分钟时不修正
 * - 可插拔：通过 bEnabled 参数启用/关闭
 */
class FTimeCorrector
{
#if WITH_DEV_AUTOMATION_TESTS
    friend class ::FTimeCorrectorDisabledTest;
    friend class ::FTimeCorrectorSmallDeltaTest;
    friend class ::FTimeCorrectorLargeDeltaTest;
    friend class ::FTimeCorrectorUpdateIntervalTest;
    friend class ::FTimeCorrectorNegativeDeltaTest;
    friend class ::FTimeCorrectorNeverUpdatedTest;
#endif

public:
    /**
     * @brief 构造函数
     * @param bInEnabled 是否启用时间校正，默认启用
     */
    explicit FTimeCorrector(bool bInEnabled = true) : bEnabled(bInEnabled), LastUpdateLocalTime(0), TimeDeltaSeconds(0)
    {
    }

    /**
     * @brief 接收服务端响应时间
     *
     * 当 HTTP 响应头中包含 x-log-time 时调用此方法。
     * 内部会计算服务端时间与本地时间的差值。
     *
     * 性能优化：如果距离上次更新小于 5 秒，则跳过本次更新。
     *
     * @param ServerUnixTimestamp 服务端 Unix 时间戳（秒）
     */
    void OnServerResponseTime(int64 ServerUnixTimestamp)
    {
        if (!bEnabled)
        {
            return;
        }

        // 获取当前本地时间
        int64 LocalNow = FDateTime::UtcNow().ToUnixTimestamp();

        // 性能优化：如果距离上次更新小于 5 秒，跳过
        int64 LastUpdate = LastUpdateLocalTime.Load(EMemoryOrder::Relaxed);
        if (LocalNow - LastUpdate < MinUpdateIntervalSeconds)
        {
            return;
        }

        // 使用 CAS 尝试更新 LastUpdateLocalTime，避免多线程重复计算
        // 如果失败说明其他线程已更新，直接返回
        if (!LastUpdateLocalTime.CompareExchange(LastUpdate, LocalNow))
        {
            return;
        }

        // 计算时间差：服务端时间 - 本地时间
        // 使用 SequentiallyConsistent 确保写入对其他线程可见
        int64 Delta = ServerUnixTimestamp - LocalNow;

        // 时间差小于阈值，不修正
        if (FMath::Abs(Delta) < MinCorrectionThresholdSeconds)
        {
            Delta = 0;
        }
        else
        {
            SLS_LOG_DEBUG("TimeCorrector: need correction, ServerTime=%lld, LocalTime=%lld, Delta=%lld",
                          ServerUnixTimestamp,
                          LocalNow,
                          Delta);
        }
        TimeDeltaSeconds.Store(Delta, EMemoryOrder::SequentiallyConsistent);
    }

    /**
     * @brief 获取校正后的 RFC1123 格式日期
     *
     * 如果时间校正已禁用，或时间差小于 1 分钟，或距离上次更新超过 3 分钟，返回本地时间。
     * 否则返回校正后的时间。
     *
     * @return RFC1123 格式的日期字符串，如 "Sun, 06 Nov 1994 08:49:37 GMT"
     */
    FString GetRfc1123Date() const
    {
        if (!bEnabled)
        {
            return FDateTime::UtcNow().ToHttpDate();
        }

        int64 LocalNow = FDateTime::UtcNow().ToUnixTimestamp();
        int64 LastUpdate = LastUpdateLocalTime.Load(EMemoryOrder::Relaxed);

        // 如果从未更新过，或距离上次更新超过 3 分钟，不修正
        if (LastUpdate == 0 || (LocalNow - LastUpdate) > MaxCorrectionAgeSeconds)
        {
            return FDateTime::UtcNow().ToHttpDate();
        }

        // 使用 SequentiallyConsistent 确保读取到最新值
        int64 Delta = TimeDeltaSeconds.Load(EMemoryOrder::SequentiallyConsistent);

        // 时间差小于 1 分钟，不修正
        if (FMath::Abs(Delta) < MinCorrectionThresholdSeconds)
        {
            return FDateTime::UtcNow().ToHttpDate();
        }

        // 应用时间校正
        FDateTime CorrectedTime = FDateTime::UtcNow() + FTimespan::FromSeconds(Delta);
        return CorrectedTime.ToHttpDate();
    }

private:
    /** 是否启用时间校正 */
    bool bEnabled;

    /** 上次更新时的本地时间戳（秒） */
    TAtomic<int64> LastUpdateLocalTime;

    /** 时间差：服务端时间 - 本地时间（秒） */
    TAtomic<int64> TimeDeltaSeconds;

    /** 最小更新间隔（秒） */
    static constexpr int64 MinUpdateIntervalSeconds = 5;

    /** 最小修正阈值（秒），小于此值不修正 */
    static constexpr int64 MinCorrectionThresholdSeconds = 60;

    /** 最大校正有效期（秒），超过此时间不使用校正 */
    static constexpr int64 MaxCorrectionAgeSeconds = 300;
};

} // namespace sls
} // namespace aliyun
