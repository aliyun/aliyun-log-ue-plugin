#include "LogGroupBuilder.h"

#include "Utils.h"

namespace aliyun
{
namespace sls
{

// Protobuf wire types
constexpr int32 WIRE_TYPE_VARINT = 0;
constexpr int32 WIRE_TYPE_LENGTH_DELIMITED = 2;

// LogGroup field numbers (参照 SLS protobuf 定义)
constexpr int32 FIELD_LOGS = 1;
constexpr int32 FIELD_TOPIC = 3;
constexpr int32 FIELD_SOURCE = 4;
constexpr int32 FIELD_LOGTAGS = 6;

// Log field numbers
constexpr int32 FIELD_LOG_TIME = 1;
constexpr int32 FIELD_LOG_CONTENTS = 2;

// Log.Content field numbers
constexpr int32 FIELD_CONTENT_KEY = 1;
constexpr int32 FIELD_CONTENT_VALUE = 2;

// LogTag field numbers
constexpr int32 FIELD_TAG_KEY = 1;
constexpr int32 FIELD_TAG_VALUE = 2;

FLogGroupBuilder::FLogGroupBuilder() : BuilderTime(FUtils::GetCurrentTimeSec()) {}

void FLogGroupBuilder::AddLog(const FLogItem& Item)
{
    EstimatedSizeBytes += Item.EstimateSize();
    Logs.Add(Item);
}

void FLogGroupBuilder::AddLog(FLogItem&& Item)
{
    EstimatedSizeBytes += Item.EstimateSize();
    Logs.Add(MoveTemp(Item));
}

void FLogGroupBuilder::SetSource(const FString& InSource)
{
    Source = InSource;
}

void FLogGroupBuilder::SetTopic(const FString& InTopic)
{
    Topic = InTopic;
}

void FLogGroupBuilder::AddTag(const FString& Key, const FString& Value)
{
    Tags.Emplace(Key, Value);
}

void FLogGroupBuilder::SetPackId(const FString& InPackId)
{
    PackId = InPackId;
}

void FLogGroupBuilder::WriteVarint(TArray<uint8>& Output, uint64 Value)
{
    while (Value > 0x7F)
    {
        Output.Add(static_cast<uint8>((Value & 0x7F) | 0x80));
        Value >>= 7;
    }
    Output.Add(static_cast<uint8>(Value));
}

void FLogGroupBuilder::WriteStringField(TArray<uint8>& Output, int32 FieldNumber, const FString& Value)
{
    // 转换为 UTF-8
    FTCHARToUTF8 Utf8(*Value);
    const int32 Utf8Len = Utf8.Length();

    // field tag = (fieldNumber << 3) | wireType
    WriteVarint(Output, (FieldNumber << 3) | WIRE_TYPE_LENGTH_DELIMITED);
    WriteVarint(Output, Utf8Len);
    Output.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8Len);
}

void FLogGroupBuilder::WriteNestedMessage(TArray<uint8>& Output, int32 FieldNumber, const TArray<uint8>& Message)
{
    WriteVarint(Output, (FieldNumber << 3) | WIRE_TYPE_LENGTH_DELIMITED);
    WriteVarint(Output, Message.Num());
    Output.Append(Message);
}

TArray<uint8> FLogGroupBuilder::SerializeLog(const FLogItem& Item) const
{
    TArray<uint8> LogData;

    // 写入时间 (field 1, varint)
    WriteVarint(LogData, (FIELD_LOG_TIME << 3) | WIRE_TYPE_VARINT);
    WriteVarint(LogData, Item.GetTime());

    // 写入内容 (field 2, repeated)
    for (const auto& Kv : Item.GetContents())
    {
        TArray<uint8> ContentData;
        WriteStringField(ContentData, FIELD_CONTENT_KEY, Kv.Key);
        WriteStringField(ContentData, FIELD_CONTENT_VALUE, Kv.Value);
        WriteNestedMessage(LogData, FIELD_LOG_CONTENTS, ContentData);
    }

    return LogData;
}

TArray<uint8> FLogGroupBuilder::Serialize() const
{
    TArray<uint8> Output;

    // 预分配空间
    Output.Reserve(EstimatedSizeBytes);

    // 写入所有日志 (field 1, repeated)
    for (const auto& Log : Logs)
    {
        TArray<uint8> LogData = SerializeLog(Log);
        WriteNestedMessage(Output, FIELD_LOGS, LogData);
    }

    // 写入 topic (field 3)
    if (!Topic.IsEmpty())
    {
        WriteStringField(Output, FIELD_TOPIC, Topic);
    }

    // 写入 source (field 4)
    if (!Source.IsEmpty())
    {
        WriteStringField(Output, FIELD_SOURCE, Source);
    }

    // 写入 tags (field 6, repeated)
    for (const auto& Tag : Tags)
    {
        TArray<uint8> TagData;
        WriteStringField(TagData, FIELD_TAG_KEY, Tag.Key);
        WriteStringField(TagData, FIELD_TAG_VALUE, Tag.Value);
        WriteNestedMessage(Output, FIELD_LOGTAGS, TagData);
    }

    // 写入 pack ID 作为 tag
    if (!PackId.IsEmpty())
    {
        TArray<uint8> TagData;
        WriteStringField(TagData, FIELD_TAG_KEY, TEXT("__pack_id__"));
        WriteStringField(TagData, FIELD_TAG_VALUE, PackId);
        WriteNestedMessage(Output, FIELD_LOGTAGS, TagData);
    }

    return Output;
}

} // namespace sls
} // namespace aliyun
