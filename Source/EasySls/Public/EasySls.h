#pragma once

#include "Modules/ModuleManager.h"
#include "SlsConfig.h"

namespace aliyun
{
namespace sls
{
class FSlsProducer;
}
} // namespace aliyun

class EASYSLS_API FEasySlsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static FEasySlsModule& Get();

    void InitGlobalProducer(const FSlsProducerConfig& Config, FSlsSendCallback Callback = nullptr);
    void ShutdownGlobalProducer();

    /**
     * @param Time Unix timestamp in seconds. 0 means use current time.
     * @param Contents Key-value pairs for the log entry.
     * @param bFlush Whether to flush immediately.
     * @return Result code defined by ESlsAddLogResult.
     */
    ESlsAddLogResult AddLog(int32 Time, const TMap<FString, FString>& Contents);

private:
    TUniquePtr<aliyun::sls::FSlsProducer> GlobalProducer;
};
