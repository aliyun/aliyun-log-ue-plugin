#include "SlsBlueprintLibrary.h"
#include "EasySls.h"

void USlsBlueprintLibrary::InitGlobalProducer(const FSlsProducerConfig& Config)
{
    FEasySlsModule::Get().InitGlobalProducer(Config);
}

void USlsBlueprintLibrary::ShutdownGlobalProducer()
{
    FEasySlsModule::Get().ShutdownGlobalProducer();
}

ESlsAddLogResult USlsBlueprintLibrary::AddLog(int32 Time, const TMap<FString, FString>& Contents)
{
    return FEasySlsModule::Get().AddLog(Time, Contents);
}
