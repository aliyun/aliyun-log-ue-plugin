#include "Utils.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "ThirdParty.h"

namespace aliyun
{
namespace sls
{

uint32 FUtils::GetCurrentTimeSec()
{
    return static_cast<uint32>(FDateTime::UtcNow().ToUnixTimestamp());
}

int64 FUtils::GetCurrentTimeMs()
{
    return thirdparty::GetUnixTimestampMs();
}

int64 FUtils::GetCurrentTimeNs()
{
    // 使用 FPlatformTime 获取高精度时间
    return static_cast<int64>(FPlatformTime::Seconds() * 1000000000.0);
}

FString FUtils::GeneratePackIdPrefix(const FString& ConfigName, const FString& Source)
{
    // 处理空字符串的默认值
    const FString& Logstore = ConfigName.IsEmpty() ? TEXT("default_logstore") : ConfigName;
    const FString& Src = Source.IsEmpty() ? TEXT("undefined") : Source;

    // 使用时间戳生成唯一前缀
    int64 Timestamp = GetCurrentTimeNs();

    FString Input = FString::Printf(TEXT("%s%s%lld"), *Logstore, *Src, Timestamp);

    // 使用 MD5 生成16字符的十六进制字符串
    FString Hash = thirdparty::MD5Hash(Input);
    // 取前16字符
    return Hash.Left(16);
}

void FUtils::SleepMs(int32 Ms)
{
    FPlatformProcess::Sleep(Ms / 1000.0f);
}

FString FUtils::FormatPackId(const FString& Prefix, uint32 Index)
{
    return FString::Printf(TEXT("%s-%X"), *Prefix, Index);
}

} // namespace sls
} // namespace aliyun
