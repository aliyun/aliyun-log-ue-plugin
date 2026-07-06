# EasySls Guide

EasySls is an Unreal plugin for sending game logs to Alibaba Cloud Log Service (SLS).
This document covers installation, basic integration, persistence configuration, and the main public parameters.

Typical use cases include:

- Client runtime log collection
- Gameplay and business event reporting
- Error and exception log reporting

## Install The Plugin

EasySls is distributed as source code and does not include prebuilt binaries. When integrating it, compile the plugin source as part of an Unreal C++ project.

First extract the plugin source package, then copy the `EasySls` folder into the `Plugins` directory of your UE project.

The directory layout after copying should look like this:

```text
<ProjectRoot>/
|-- Plugins/
|   `-- EasySls/
|       |-- EasySls.uplugin
|       |-- Config/
|       |-- Resources/
|       `-- Source/
`-- Source/
```

## Build And Enable

After copying the plugin, use either of the following ways to build and load it.

1. Build and load through Unreal Editor (recommended)

- Open the Unreal C++ project. If the editor prompts you to build the plugin or project modules, follow the prompt and start the build.
- After the build completes, open `Edit -> Plugins`, search for `EasySls`, and make sure it is enabled. If the editor asks for a restart, restart it.

2. Build from the command line, then load it in Unreal Editor

- Build the project's editor target first, for example:

```bash
<UE_PATH>/Engine/Build/BatchFiles/<Platform>/Build.* <ProjectEditorTarget> <Platform> Development <Project.uproject>
```

- After the build completes, open Unreal Editor, go to `Edit -> Plugins`, search for `EasySls`, and make sure it is enabled. If the editor asks for a restart, restart it.

## Using The SLS Plugin

Integrate it with the following flow:

- Initialize the global producer during startup
- Call `AddLog` in gameplay or business code
- Shut down the global producer during exit

### Initialization

Use the following example to initialize the global producer. Typical entry points include `GameInstance::Init()`, a module startup function, or another global initialization path.

```cpp
#include "EasySls.h"

void FYourSystem::InitSls()
{
    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("cn-hangzhou.log.aliyuncs.com"); // SLS endpoint
    Config.Project = TEXT("your-project"); // SLS project name
    Config.Logstore = TEXT("your-logstore"); // SLS logstore name
    Config.AccessKeyId = TEXT("your-ak"); // Alibaba Cloud AccessKey ID
    Config.AccessKeySecret = TEXT("your-sk"); // Alibaba Cloud AccessKey Secret

    FEasySlsModule::Get().InitGlobalProducer(Config); // Initialize the global producer
}
```

### Write Logs

```cpp
void FYourSystem::WriteSlsLog()
{
    TMap<FString, FString> Contents;
    Contents.Add(TEXT("level"), TEXT("INFO"));
    Contents.Add(TEXT("event"), TEXT("player_login"));
    Contents.Add(TEXT("player_id"), TEXT("10001"));
    Contents.Add(TEXT("map"), TEXT("lobby"));

    const int32 UnixTime = 1712995200; // Unix timestamp in seconds
    FEasySlsModule::Get().AddLog(UnixTime, Contents);
}
```

### Shutdown

Call the following API during shutdown:

```cpp
void FYourSystem::ShutdownSls()
{
    FEasySlsModule::Get().ShutdownGlobalProducer();
}
```

## Persistence

To keep unsent logs during network issues or unexpected client shutdowns and recover them later, enable persistence during initialization as shown below.

```cpp
void FYourSystem::InitSlsWithPersistence()
{
    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("cn-hangzhou.log.aliyuncs.com");
    Config.Project = TEXT("your-project");
    Config.Logstore = TEXT("your-logstore");
    Config.AccessKeyId = TEXT("your-ak");
    Config.AccessKeySecret = TEXT("your-sk");

    Config.bEnablePersistence = true; // Enable persistence
    Config.PersistenceDir = TEXT("Saved/SlsPersistence"); // Local directory for persisted logs

    FEasySlsModule::Get().InitGlobalProducer(Config);
}
```

Notes:

- `bEnablePersistence`: whether persistence is enabled
- `PersistenceDir`: local directory for persisted log data, which must be readable and writable

## Blueprint Integration

Blueprint projects can directly use the following APIs provided by the plugin:

- `InitGlobalProducer`
- `AddLog`
- `ShutdownGlobalProducer`

The integration order is the same as in C++:

1. Initialize on startup
2. Write logs while running
3. Shut down on exit

## Parameter Reference

The table below lists the main public configuration parameters.

| Parameter | Required | Description | Default / Notes |
| --- | --- | --- | --- |
| `Endpoint` | Yes | SLS endpoint | For example `cn-hangzhou.log.aliyuncs.com` |
| `Project` | Yes | SLS project name | No default |
| `Logstore` | Yes | SLS logstore name | No default |
| `AccessKeyId` | Yes | Alibaba Cloud AccessKey ID | No default |
| `AccessKeySecret` | Yes | Alibaba Cloud AccessKey Secret | No default |
| `SecurityToken` | No | STS security token | Empty by default; set when using STS credentials |
| `Source` | No | Log source | Empty by default; commonly set to `clientIp` |
| `Topic` | No | Log topic | Empty by default |
| `Tags` | No | Log tags | Empty by default |
| `bUsingHttps` | No | Whether to use HTTPS | Default `true` |
| `CompressType` | No | Compression type | Default `Lz4` |
| `bEnablePersistence` | No | Whether to enable persistence | Default `false` |
| `PersistenceDir` | No | Local directory for persisted logs | Empty by default; required when persistence is enabled |
| `MaxPersistentFileSize` | No | Maximum size of a single persisted file in bytes | Default `10485760` (10 MB) |
| `MaxPersistentFileCount` | No | Maximum number of persisted files | Default `10` |
| `MaxPersistenceTimeSec` | No | Maximum retention time for persisted files in seconds | Default `604800` (7 days) |

## Return Values

`AddLog` returns values from `ESlsResultCode`. Common return values include:

- `ESlsResultCode::Ok`: the log was accepted by the producer
- `ESlsResultCode::MemoryReachLimit`: the producer hit its memory limit and the log was dropped
- `ESlsResultCode::ProducerNotExist`: the global producer has not been initialized

For the full definition, see:
[SlsConfig.h](Source/EasySls/Public/SlsConfig.h)
