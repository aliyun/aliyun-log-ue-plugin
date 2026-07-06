#pragma once

// clang-format off
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoreMinimal.h"
#include "SlsConfig.h"
#include "SlsBlueprintLibrary.generated.h"
// clang-format on

UCLASS()
class EASYSLS_API USlsBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "SLS", meta = (ToolTip = "Initialize the global SLS Producer"))
    static void InitGlobalProducer(const FSlsProducerConfig& Config);

    UFUNCTION(BlueprintCallable, Category = "SLS", meta = (ToolTip = "Shutdown the global SLS Producer"))
    static void ShutdownGlobalProducer();

    /**
     * @param Time Unix timestamp in seconds. 0 means use current time.
     * @param Contents Key-value pairs for the log entry.
     * @param bFlush Whether to flush immediately.
     * @return Result code defined by ESlsAddLogResult.
     */
    UFUNCTION(BlueprintCallable, Category = "SLS", meta = (ToolTip = "Add a log to the global SLS Producer"))
    static ESlsAddLogResult AddLog(int32 Time, const TMap<FString, FString>& Contents);
};
