# EasySls 使用说明

EasySls 用于将 Unreal 游戏中的日志数据发送到阿里云日志服务 SLS。
本文档说明 EasySls 插件的安装方式、基础接入流程、断点续传配置以及主要参数含义。

适用场景包括：

- 客户端运行日志采集
- 业务埋点日志上报
- 异常与错误日志上报

## 安装插件

EasySls 按源码形式分发，不包含预编译二进制。在接入插件时，需要在 Unreal C++ 项目中完成插件源码编译。

先解压插件源码包，再将 `EasySls` 目录拷贝到你的 UE 项目的 `Plugins` 目录中。

拷贝后的目录结构示例如下：

```text
<项目目录>/
|-- Plugins/
|   `-- EasySls/
|       |-- EasySls.uplugin
|       |-- Config/
|       |-- Resources/
|       `-- Source/
`-- Source/
```

## 编译与启用

插件拷贝完成后，可按以下两种方式任选其一完成编译与加载。

1. 通过 Unreal Editor 编译并加载（推荐）

    - 打开 Unreal C++ 项目，如编辑器提示需要编译插件或模块，按提示执行编译
    - 编译完成后进入 `Edit -> Plugins`， 搜索 `EasySls` 并确认插件已启用。如提示重启编辑器，按提示重启

2. 通过命令行编译，再进入编辑器加载

    - 先编译项目对应的 Editor Target，例如：

    ```bash
    <UE_PATH>/Engine/Build/BatchFiles/<Platform>/Build.* <ProjectEditorTarget> <Platform> Development <Project.uproject>
    ```

    - 编译完成后打开 Unreal Editor，进入 `Edit -> Plugins`，搜索 `EasySls` 并确认插件已启用。如提示重启编辑器，按提示重启

## 使用 SLS 插件

按以下方式接入：

- 在启动阶段初始化全局 Producer 实例
- 在业务代码中调用 `AddLog` 写入日志
- 在退出阶段关闭全局 Producer 实例

### 初始化

以下示例用于初始化全局 Producer。示例代码可放在启动逻辑中，例如 `GameInstance::Init()`、模块启动函数或其他全局初始化入口。

```cpp
#include "EasySls.h"

void FYourSystem::InitSls()
{
    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("cn-hangzhou.log.aliyuncs.com"); // SLS 服务地址
    Config.Project = TEXT("your-project"); // SLS Project 名称
    Config.Logstore = TEXT("your-logstore"); // SLS Logstore 名称
    Config.AccessKeyId = TEXT("your-ak"); // 阿里云访问凭证 ID
    Config.AccessKeySecret = TEXT("your-sk"); // 阿里云访问凭证 Secret

    FEasySlsModule::Get().InitGlobalProducer(Config); // 初始化全局 Producer 实例
}
```

### 写日志


```cpp
void FYourSystem::WriteSlsLog()
{
    TMap<FString, FString> Contents;
    Contents.Add(TEXT("level"), TEXT("INFO"));
    Contents.Add(TEXT("event"), TEXT("player_login"));
    Contents.Add(TEXT("player_id"), TEXT("10001"));
    Contents.Add(TEXT("map"), TEXT("lobby"));

    const int32 UnixTime = 1712995200; // 日志时间，Unix 时间戳，单位为秒
    FEasySlsModule::Get().AddLog(UnixTime, Contents);
}
```

### 关闭

在退出逻辑中调用以下接口关闭全局 Producer：

```cpp
void FYourSystem::ShutdownSls()
{
    FEasySlsModule::Get().ShutdownGlobalProducer();
}
```

## 断点续传

如需在网络波动、客户端异常退出等场景下保留未发送日志并在后续恢复发送，可按以下初始化方法，启用断点续传。


```cpp
void FYourSystem::InitSlsWithPersistence()
{
    FSlsProducerConfig Config;
    Config.Endpoint = TEXT("cn-hangzhou.log.aliyuncs.com");
    Config.Project = TEXT("your-project");
    Config.Logstore = TEXT("your-logstore");
    Config.AccessKeyId = TEXT("your-ak");
    Config.AccessKeySecret = TEXT("your-sk");

    Config.bEnablePersistence = true; // 开启断点续传
    Config.PersistenceDir = TEXT("Saved/SlsPersistence"); // 设置日志的本地保存目录

    FEasySlsModule::Get().InitGlobalProducer(Config);
}
```

说明：

- `bEnablePersistence` 是否启用断点续传
- `PersistenceDir` 断点续传日志的本地保存目录，必须要有可读写权限

## Blueprint 接入

Blueprint 项目可直接使用插件提供的以下接口：

- `InitGlobalProducer`
- `AddLog`
- `ShutdownGlobalProducer`

接入顺序与 C++ 一致：

1. 启动时初始化
2. 运行过程中写入日志
3. 退出时关闭

## 参数说明

下表列出对外暴露的主要配置参数。

| 参数 | 是否必填 | 说明 | 默认值 / 备注 |
| --- | --- | --- | --- |
| `Endpoint` | 是 | SLS 服务地址 | 例如 `cn-hangzhou.log.aliyuncs.com` |
| `Project` | 是 | SLS Project 名称 | 无默认值 |
| `Logstore` | 是 | SLS Logstore 名称 | 无默认值 |
| `AccessKeyId` | 是 | 阿里云访问凭证 ID | 无默认值 |
| `AccessKeySecret` | 是 | 阿里云访问凭证 Secret | 无默认值 |
| `SecurityToken` | 否 | STS 安全令牌 | 默认空；使用 STS 鉴权时填写 |
| `Source` | 否 | 日志来源 | 默认空；一般设置为 `clientIp` |
| `Topic` | 否 | 日志主题 | 默认空 |
| `Tags` | 否 | 日志标签 | 默认空 |
| `bUsingHttps` | 否 | 是否使用 HTTPS | 默认 `true` |
| `CompressType` | 否 | 压缩方式 | 默认 `Lz4` |
| `bEnablePersistence` | 否 | 是否启用断点续传 | 默认 `false` |
| `PersistenceDir` | 否 | 断点续传日志本地保存目录 | 默认空；启用断点续传时必填 |
| `MaxPersistentFileSize` | 否 | 单个断点续传文件最大大小，单位：字节（Byte） | 默认 `10485760`（10 MB） |
| `MaxPersistentFileCount` | 否 | 最大断点续传文件数量 | 默认 `10` |
| `MaxPersistenceTimeSec` | 否 | 断点续传文件最大保留时间，单位：秒 | 默认 `604800`（7 天） |

## 返回值说明

`AddLog` 返回 `ESlsResultCode` 枚举值，常用返回值如下：

- `ESlsResultCode::Ok`：日志成功进入 Producer
- `ESlsResultCode::MemoryReachLimit`：当前 Producer 内存达到限制，日志被丢弃
- `ESlsResultCode::ProducerNotExist`：全局 Producer 尚未初始化


完整返回码定义见：
[SlsConfig.h](Source/EasySls/Public/SlsConfig.h)
