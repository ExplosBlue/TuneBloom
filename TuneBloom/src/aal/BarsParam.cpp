#include <aal/BarsParam.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>

#include <cstring>
#include <unordered_map>

namespace aal
{

static constexpr char cParamMagic[]        = "BARS";
static constexpr u16 cByteOrderMark        = 0xFEFF;
static constexpr u32 cHeaderByteOrder      = 0x04;
static constexpr u32 cHeaderSizeField      = 0x06;
static constexpr u32 cHeaderFileSize       = 0x08;
static constexpr u32 cHeaderRecordSize     = 0x0C;
static constexpr u32 cHeaderRecordsOffset  = 0x10;
static constexpr u32 cHeaderRecordsSize    = 0x14;
static constexpr u32 cHeaderPoolOffset     = 0x18;
static constexpr u32 cHeaderPoolSize       = 0x1C;
static constexpr u32 cHeaderSize           = 0x20;
static constexpr u32 cRecordSize           = (cBarsParamTextCount + cBarsParamValueCount) * sizeof(u32);

bool ReadBarsParam(const std::vector<u8>& data, BarsParam* out)
{
    if (data.size() < cHeaderSize || std::memcmp(data.data(), cParamMagic, sizeof(u32)) != 0 || !ByteView::IsByteOrderMark(data.data(), data.size(), cHeaderByteOrder))
        return false;

    const ByteView view(data.data(), data.size(), ByteView::IsBigEndianMark(data.data(), data.size(), cHeaderByteOrder));
    const u64 records = view.readU32(cHeaderRecordsOffset);
    const u64 recordsSize = view.readU32(cHeaderRecordsSize);
    const u64 pool = view.readU32(cHeaderPoolOffset);
    const u64 poolSize = view.readU32(cHeaderPoolSize);

    if (view.readU16(cHeaderSizeField) != cHeaderSize || view.readU32(cHeaderFileSize) != data.size() || view.readU32(cHeaderRecordSize) != cRecordSize ||
        records != cHeaderSize || recordsSize % cRecordSize != 0 || pool != records + recordsSize || pool + poolSize != data.size())
        return false;

    BarsParam param;
    param.isBigEndian = view.isBigEndian();
    param.records.resize(recordsSize / cRecordSize);

    for (u64 i = 0; i < param.records.size(); i++)
    {
        BarsParamRecord& record = param.records[i];
        const u64 offset = records + i * cRecordSize;

        for (u32 j = 0; j < cBarsParamTextCount; j++)
        {
            if (!view.readCString(pool + view.readU32(offset + j * sizeof(u32)), pool + poolSize, &record.texts[j]))
                return false;
        }

        for (u32 j = 0; j < cBarsParamValueCount; j++)
            record.values[j] = view.readU32(offset + (cBarsParamTextCount + j) * sizeof(u32));
    }

    *out = std::move(param);
    return true;
}

std::vector<u8> WriteBarsParam(const BarsParam& param)
{
    std::vector<u8> pool;
    std::unordered_map<std::string, u32> offsets;

    const auto intern = [&pool, &offsets](const std::string& text)
    {
        const auto found = offsets.find(text);

        if (found != offsets.end())
            return found->second;

        const u32 offset = static_cast<u32>(pool.size());
        offsets.emplace(text, offset);
        pool.insert(pool.end(), text.begin(), text.end());
        pool.push_back(0);

        return offset;
    };

    const u32 recordsSize = static_cast<u32>(param.records.size()) * cRecordSize;
    ByteBuilder builder(param.isBigEndian);
    builder.resize(cHeaderSize);

    for (const BarsParamRecord& record : param.records)
    {
        for (const std::string& text : record.texts)
            builder.appendU32(intern(text));

        for (u32 value : record.values)
            builder.appendU32(value);
    }

    builder.appendBytes(pool.data(), pool.size());

    builder.putBytes(0, cParamMagic, sizeof(u32));
    builder.putU16(cHeaderByteOrder, cByteOrderMark);
    builder.putU16(cHeaderSizeField, cHeaderSize);
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));
    builder.putU32(cHeaderRecordSize, cRecordSize);
    builder.putU32(cHeaderRecordsOffset, cHeaderSize);
    builder.putU32(cHeaderRecordsSize, recordsSize);
    builder.putU32(cHeaderPoolOffset, cHeaderSize + recordsSize);
    builder.putU32(cHeaderPoolSize, static_cast<u32>(pool.size()));

    return builder.take();
}

}
