#pragma once

#include <string>
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Compression.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/SecureHash.h"
#include "CoreMinimal.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"

namespace aliyun
{
namespace sls
{
namespace thirdparty
{

/**
 * @brief 第三方依赖接口 - UE 原生实现
 *
 * 本模块使用 Unreal Engine 原生 API 实现所有外部依赖功能。
 */

// ============================================================================
// 编码相关
// ============================================================================

/**
 * @brief 十六进制编码
 * @param Data 输入数据
 * @return 十六进制字符串（小写）
 */
inline FString HexEncode(const TArray<uint8>& Data)
{
    return BytesToHex(Data.GetData(), Data.Num()).ToLower();
}

inline FString HexEncode(const FString& Data)
{
    TArray<uint8> Bytes;
    FTCHARToUTF8 Utf8(*Data);
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    return HexEncode(Bytes);
}

/**
 * @brief URL 编码
 * @param Value 输入字符串
 * @return URL 编码后的字符串
 */
inline FString UrlEncode(const FString& Value)
{
    FString Result;
    FTCHARToUTF8 Utf8(*Value);
    const char* Ptr = Utf8.Get();
    const int32 Len = Utf8.Length();

    for (int32 i = 0; i < Len; ++i)
    {
        uint8 Ch = static_cast<uint8>(Ptr[i]);

        // 不编码：字母、数字、- _ . ~
        if ((Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9') || Ch == '-' ||
            Ch == '_' || Ch == '.' || Ch == '~')
        {
            Result.AppendChar(Ch);
        }
        else
        {
            // 编码为 %XX 格式
            Result.Appendf(TEXT("%%%02X"), Ch);
        }
    }

    return Result;
}

/**
 * @brief Base64 编码
 * @param Input 输入数据
 * @return Base64 编码字符串
 */
inline FString Base64Encode(const TArray<uint8>& Input)
{
    return FBase64::Encode(Input);
}

inline FString Base64Encode(const FString& Input)
{
    return FBase64::Encode(Input);
}

// ============================================================================
// 哈希和签名相关
// ============================================================================

/**
 * @brief MD5 哈希
 * @param Data 输入数据
 * @return MD5 哈希值（32字符十六进制字符串，大写）
 */
inline FString MD5Hash(const FString& Data)
{
    return FMD5::HashAnsiString(*Data).ToUpper();
}

inline FString MD5Hash(const TArray<uint8>& Data)
{
    FMD5 Md5Gen;
    Md5Gen.Update(Data.GetData(), Data.Num());

    uint8 Digest[16];
    Md5Gen.Final(Digest);

    return BytesToHex(Digest, 16).ToUpper();
}

/**
 * @brief SHA1 哈希
 * @param Data 输入数据
 * @return SHA1 哈希（20字节）
 */
inline TArray<uint8> Sha1Hash(const TArray<uint8>& Data)
{
    FSHA1 Sha1;
    Sha1.Update(Data.GetData(), Data.Num());
    Sha1.Final();

    TArray<uint8> Result;
    Result.SetNumUninitialized(20);
    Sha1.GetHash(Result.GetData());

    return Result;
}

/**
 * @brief HMAC-SHA1 签名
 * @param Key 密钥
 * @param Data 待签名数据
 * @return HMAC-SHA1 签名（二进制数组，20字节）
 */
inline TArray<uint8> HmacSha1(const TArray<uint8>& Key, const TArray<uint8>& Data)
{
    TArray<uint8> OutHash;
    OutHash.SetNumUninitialized(20); // SHA1 输出 20 字节

    FSHA1::HMACBuffer(Key.GetData(), Key.Num(), Data.GetData(), Data.Num(), OutHash.GetData());

    return OutHash;
}

inline TArray<uint8> HmacSha1(const FString& Key, const FString& Data)
{
    TArray<uint8> KeyBytes;
    TArray<uint8> DataBytes;

    FTCHARToUTF8 KeyUtf8(*Key);
    FTCHARToUTF8 DataUtf8(*Data);

    KeyBytes.Append(reinterpret_cast<const uint8*>(KeyUtf8.Get()), KeyUtf8.Length());
    DataBytes.Append(reinterpret_cast<const uint8*>(DataUtf8.Get()), DataUtf8.Length());

    return HmacSha1(KeyBytes, DataBytes);
}


// ============================================================================
// 压缩相关
// ============================================================================

/**
 * @brief LZ4 压缩
 * @param Input 原始数据
 * @param OutCompressed 压缩后的数据
 * @return 压缩是否成功
 */
inline bool Lz4Compress(const TArray<uint8>& Input, TArray<uint8>& OutCompressed)
{
    int32 CompressedSize = FCompression::CompressMemoryBound(NAME_LZ4, Input.Num());
    OutCompressed.SetNumUninitialized(CompressedSize);

    if (FCompression::CompressMemory(NAME_LZ4, OutCompressed.GetData(), CompressedSize, Input.GetData(), Input.Num()))
    {
        OutCompressed.SetNum(CompressedSize);
        return true;
    }

    return false;
}

/**
 * @brief LZ4 解压
 * @param Input 压缩数据
 * @param OriginalSize 原始数据大小
 * @param OutDecompressed 解压后的数据
 * @return 解压是否成功
 */
inline bool Lz4Decompress(const TArray<uint8>& Input, int32 OriginalSize, TArray<uint8>& OutDecompressed)
{
    OutDecompressed.SetNumUninitialized(OriginalSize);

    return FCompression::UncompressMemory(NAME_LZ4,
                                          OutDecompressed.GetData(),
                                          OriginalSize,
                                          Input.GetData(),
                                          Input.Num());
}

/**
 * @brief 检查 LZ4 是否可用
 */
inline bool IsLz4Available()
{
    return FCompression::IsFormatValid(NAME_LZ4);
}

// ============================================================================
// 日期时间相关
// ============================================================================

/**
 * @brief 获取 RFC1123 格式的日期字符串
 * @return 格式如 "Sun, 06 Nov 1994 08:49:37 GMT"
 */
inline FString GetRfc1123Date()
{
    return FDateTime::UtcNow().ToHttpDate();
}

/**
 * @brief 获取当前 Unix 时间戳（秒）
 */
inline int64 GetUnixTimestamp()
{
    return FDateTime::UtcNow().ToUnixTimestamp();
}

/**
 * @brief 获取当前 Unix 时间戳（毫秒）
 */
inline int64 GetUnixTimestampMs()
{
    return (FDateTime::UtcNow().GetTicks() - FDateTime(1970, 1, 1).GetTicks()) / ETimespan::TicksPerMillisecond;
}

// ============================================================================
// 字符串工具
// ============================================================================

/**
 * @brief 转换为小写
 * @param Str 输入字符串
 * @return 小写字符串
 */
inline FString ToLowerCase(const FString& Str)
{
    return Str.ToLower();
}

/**
 * @brief 转换为大写
 * @param Str 输入字符串
 * @return 大写字符串
 */
inline FString ToUpperCase(const FString& Str)
{
    return Str.ToUpper();
}

// ============================================================================
// 网络相关
// ============================================================================

/**
 * @brief 获取本机 IP 地址
 * @return IP 地址字符串
 */
inline FString GetLocalIp()
{
    bool bCanBindAll = false;
    FOutputDeviceNull NullOut;
    TSharedPtr<FInternetAddr> LocalAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
                                              ->GetLocalHostAddr(NullOut, bCanBindAll);

    if (LocalAddr.IsValid() && LocalAddr->IsValid())
    {
        return LocalAddr->ToString(false); // false = 不包含端口
    }

    return TEXT("127.0.0.1");
}

// ============================================================================
// 异步工具
// ============================================================================

/**
 * @brief 延迟执行函数（在后台线程）
 * @param Func 要执行的函数
 * @param DelayMs 延迟时间（毫秒）
 */
template <typename FuncType>
void DelayRun(FuncType&& Func, int32 DelayMs)
{
    auto FuncPtr = MakeShared<typename TRemoveReference<FuncType>::Type>(Forward<FuncType>(Func));

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
              [FuncPtr, DelayMs]()
              {
                  if (DelayMs > 0)
                  {
                      FPlatformProcess::Sleep(DelayMs / 1000.0f);
                  }
                  (*FuncPtr)();
              });
}


// ============================================================================
// 转换工具（FString <-> std::string 兼容层）
// ============================================================================

/**
 * @brief FString 转 std::string (UTF-8)
 */
inline std::string ToStdString(const FString& Str)
{
    FTCHARToUTF8 Utf8(*Str);
    return std::string(Utf8.Get(), Utf8.Length());
}

/**
 * @brief std::string (UTF-8) 转 FString
 */
inline FString ToFString(const std::string& Str)
{
    return UTF8_TO_TCHAR(Str.c_str());
}

/**
 * @brief TArray<uint8> 转 std::string
 */
inline std::string ToStdString(const TArray<uint8>& Data)
{
    return std::string(reinterpret_cast<const char*>(Data.GetData()), Data.Num());
}

/**
 * @brief std::string 转 TArray<uint8>
 */
inline TArray<uint8> ToByteArray(const std::string& Str)
{
    TArray<uint8> Result;
    Result.Append(reinterpret_cast<const uint8*>(Str.data()), Str.size());
    return Result;
}

/**
 * @brief FString 转 TArray<uint8> (UTF-8)
 */
inline TArray<uint8> ToByteArray(const FString& Str)
{
    TArray<uint8> Result;
    FTCHARToUTF8 Utf8(*Str);
    Result.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    return Result;
}

} // namespace thirdparty
} // namespace sls
} // namespace aliyun
