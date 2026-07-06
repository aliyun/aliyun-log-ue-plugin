#include "EasySls.h"
#include "Flusher.h"
#include "LogItem.h"
#include "Producer.h"

void FEasySlsModule::StartupModule() {}

void FEasySlsModule::ShutdownModule()
{
    ShutdownGlobalProducer();
}

FEasySlsModule& FEasySlsModule::Get()
{
    return FModuleManager::GetModuleChecked<FEasySlsModule>("EasySls");
}

void FEasySlsModule::InitGlobalProducer(const FSlsProducerConfig& Config, FSlsSendCallback Callback)
{
    ShutdownGlobalProducer();

    GlobalProducer = MakeUnique<aliyun::sls::FSlsProducer>(Config, MoveTemp(Callback));
    if (!GlobalProducer->Init())
    {
        UE_LOG(LogTemp, Error, TEXT("FEasySlsModule: Failed to init GlobalProducer"));
        GlobalProducer.Reset();
    }
}

void FEasySlsModule::ShutdownGlobalProducer()
{
    GlobalProducer.Reset();
}

ESlsAddLogResult FEasySlsModule::AddLog(int32 Time, const TMap<FString, FString>& Contents)
{
    if (!GlobalProducer)
    {
        return ESlsAddLogResult::ProducerNotExist;
    }

    aliyun::sls::FLogItem Item;
    if (Time > 0)
    {
        Item.SetTime(static_cast<uint32>(Time));
    }
    for (const auto& Pair : Contents)
    {
        Item.AddContent(Pair.Key, Pair.Value);
    }

    return GlobalProducer->AddLog(MoveTemp(Item));
}

IMPLEMENT_MODULE(FEasySlsModule, EasySls)
