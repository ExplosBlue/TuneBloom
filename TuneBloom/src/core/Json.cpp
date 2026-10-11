#include <core/Json.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json
{

static constexpr u32 cMaxDepth           = 64;
static constexpr u32 cIndentWidth        = 2;
static constexpr s32 cSinglePrecisionMax = 9;
static constexpr s32 cDoublePrecisionMax = 17;
static constexpr f64 cIntegerMax         = 9007199254740992.0;

struct Cursor
{
    const char* pos;
    const char* end;
};

static bool ParseValue(Cursor* cursor, Value* out, u32 depth);

static void SkipWhitespace(Cursor* cursor)
{
    while (cursor->pos < cursor->end)
    {
        const char c = *cursor->pos;

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            return;

        cursor->pos++;
    }
}

static bool Consume(Cursor* cursor, char c)
{
    if (cursor->pos >= cursor->end || *cursor->pos != c)
        return false;

    cursor->pos++;

    return true;
}

static bool ConsumeWord(Cursor* cursor, const char* word)
{
    const size_t length = std::strlen(word);

    if (static_cast<size_t>(cursor->end - cursor->pos) < length || std::memcmp(cursor->pos, word, length) != 0)
        return false;

    cursor->pos += length;

    return true;
}

static bool ParseHex4(Cursor* cursor, u32* out)
{
    if (cursor->end - cursor->pos < 4)
        return false;

    u32 value = 0;

    for (u32 i = 0; i < 4; i++)
    {
        const char c = cursor->pos[i];
        u32 digit;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return false;

        value = (value << 4) | digit;
    }

    cursor->pos += 4;
    *out = value;

    return true;
}

static void AppendUtf8(std::string* out, u32 codePoint)
{
    if (codePoint < 0x80)
    {
        *out += static_cast<char>(codePoint);
    }
    else if (codePoint < 0x800)
    {
        *out += static_cast<char>(0xC0 | (codePoint >> 6));
        *out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else if (codePoint < 0x10000)
    {
        *out += static_cast<char>(0xE0 | (codePoint >> 12));
        *out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        *out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else
    {
        *out += static_cast<char>(0xF0 | (codePoint >> 18));
        *out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        *out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        *out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

static bool ParseString(Cursor* cursor, std::string* out)
{
    if (!Consume(cursor, '"'))
        return false;

    out->clear();

    while (cursor->pos < cursor->end)
    {
        const char c = *cursor->pos++;

        if (c == '"')
            return true;

        if (c != '\\')
        {
            *out += c;
            continue;
        }

        if (cursor->pos >= cursor->end)
            return false;

        const char escape = *cursor->pos++;

        switch (escape)
        {
        case '"':
        case '\\':
        case '/':
            *out += escape;
            break;
        case 'b':
            *out += '\b';
            break;
        case 'f':
            *out += '\f';
            break;
        case 'n':
            *out += '\n';
            break;
        case 'r':
            *out += '\r';
            break;
        case 't':
            *out += '\t';
            break;
        case 'u':
        {
            u32 codePoint;

            if (!ParseHex4(cursor, &codePoint))
                return false;

            if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
            {
                u32 low;

                if (!ConsumeWord(cursor, "\\u") || !ParseHex4(cursor, &low) || low < 0xDC00 || low > 0xDFFF)
                    return false;

                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
            }
            else if (codePoint == 0 || (codePoint >= 0xDC00 && codePoint <= 0xDFFF))
            {
                return false;
            }

            AppendUtf8(out, codePoint);
            break;
        }
        default:
            return false;
        }
    }

    return false;
}

static bool ParseNumber(Cursor* cursor, Value* out)
{
    std::string text;

    while (cursor->pos < cursor->end)
    {
        const char c = *cursor->pos;

        if ((c < '0' || c > '9') && c != '-' && c != '+' && c != '.' && c != 'e' && c != 'E')
            break;

        text += c;
        cursor->pos++;
    }

    if (text.empty())
        return false;

    char* end = nullptr;
    const f64 number = std::strtod(text.c_str(), &end);

    if (end != text.c_str() + text.size())
        return false;

    out->type = Type::Number;
    out->number = number;

    return true;
}

static bool ParseArray(Cursor* cursor, Value* out, u32 depth)
{
    out->type = Type::Array;

    SkipWhitespace(cursor);

    if (Consume(cursor, ']'))
        return true;

    while (true)
    {
        out->items.emplace_back();

        if (!ParseValue(cursor, &out->items.back(), depth + 1))
            return false;

        SkipWhitespace(cursor);

        if (Consume(cursor, ']'))
            return true;

        if (!Consume(cursor, ','))
            return false;
    }
}

static bool ParseObject(Cursor* cursor, Value* out, u32 depth)
{
    out->type = Type::Object;

    SkipWhitespace(cursor);

    if (Consume(cursor, '}'))
        return true;

    while (true)
    {
        SkipWhitespace(cursor);

        out->members.emplace_back();
        Member& member = out->members.back();

        if (!ParseString(cursor, &member.key))
            return false;

        SkipWhitespace(cursor);

        if (!Consume(cursor, ':'))
            return false;

        if (!ParseValue(cursor, &member.value, depth + 1))
            return false;

        SkipWhitespace(cursor);

        if (Consume(cursor, '}'))
            return true;

        if (!Consume(cursor, ','))
            return false;
    }
}

static bool ParseValue(Cursor* cursor, Value* out, u32 depth)
{
    if (depth > cMaxDepth)
        return false;

    SkipWhitespace(cursor);

    if (cursor->pos >= cursor->end)
        return false;

    switch (*cursor->pos)
    {
    case '{':
        cursor->pos++;
        return ParseObject(cursor, out, depth);
    case '[':
        cursor->pos++;
        return ParseArray(cursor, out, depth);
    case '"':
        out->type = Type::String;
        return ParseString(cursor, &out->string);
    case 't':
        out->type = Type::Bool;
        out->boolean = true;
        return ConsumeWord(cursor, "true");
    case 'f':
        out->type = Type::Bool;
        out->boolean = false;
        return ConsumeWord(cursor, "false");
    case 'n':
        out->type = Type::Null;
        return ConsumeWord(cursor, "null");
    default:
        return ParseNumber(cursor, out);
    }
}

bool Parse(const char* text, size_t size, Value* out)
{
    Cursor cursor{ text, text + size };

    if (size >= 3 && std::memcmp(text, "\xEF\xBB\xBF", 3) == 0)
        cursor.pos += 3;

    *out = Value();

    if (!ParseValue(&cursor, out, 0))
        return false;

    SkipWhitespace(&cursor);

    return cursor.pos == cursor.end;
}

void AppendString(std::string* out, const char* text)
{
    *out += '"';

    for (const char* c = text; *c != '\0'; c++)
    {
        switch (*c)
        {
        case '"':
            *out += "\\\"";
            break;
        case '\\':
            *out += "\\\\";
            break;
        case '\n':
            *out += "\\n";
            break;
        case '\r':
            *out += "\\r";
            break;
        case '\t':
            *out += "\\t";
            break;
        default:
            if (static_cast<u8>(*c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<u8>(*c));
                *out += buf;
            }
            else
            {
                *out += *c;
            }
            break;
        }
    }

    *out += '"';
}

static f64 RoundToSingleText(f32 value)
{
    char text[32];
    f64 number = value;

    for (s32 precision = 1; precision <= cSinglePrecisionMax; precision++)
    {
        std::snprintf(text, sizeof(text), "%.*g", precision, value);
        number = std::strtod(text, nullptr);

        if (static_cast<f32>(number) == value)
            break;
    }

    return number;
}

static void AppendNumber(std::string* out, f64 number)
{
    if (!std::isfinite(number))
    {
        *out += "null";
        return;
    }

    char text[32];

    if (std::floor(number) == number && std::fabs(number) <= cIntegerMax)
    {
        std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(number));
        *out += text;
        return;
    }

    for (s32 precision = 1; precision <= cDoublePrecisionMax; precision++)
    {
        std::snprintf(text, sizeof(text), "%.*g", precision, number);

        if (std::strtod(text, nullptr) == number)
            break;
    }

    *out += text;
}

static bool IsNumberList(const Value& value)
{
    for (const Value& item : value.items)
    {
        if (item.type != Type::Number)
            return false;
    }

    return true;
}

static void AppendValue(std::string* out, const Value& value, u32 depth);

static void AppendArray(std::string* out, const Value& value, u32 depth)
{
    if (value.items.empty())
    {
        *out += "[]";
        return;
    }

    const bool oneLine = IsNumberList(value);

    *out += '[';

    for (size_t i = 0; i < value.items.size(); i++)
    {
        if (i != 0)
            *out += oneLine ? ", " : ",";

        if (!oneLine)
        {
            *out += '\n';
            out->append((depth + 1) * cIndentWidth, ' ');
        }

        AppendValue(out, value.items[i], depth + 1);
    }

    if (!oneLine)
    {
        *out += '\n';
        out->append(depth * cIndentWidth, ' ');
    }

    *out += ']';
}

static void AppendObject(std::string* out, const Value& value, u32 depth)
{
    if (value.members.empty())
    {
        *out += "{}";
        return;
    }

    *out += '{';

    for (size_t i = 0; i < value.members.size(); i++)
    {
        if (i != 0)
            *out += ',';

        *out += '\n';
        out->append((depth + 1) * cIndentWidth, ' ');

        AppendString(out, value.members[i].key.c_str());
        *out += ": ";
        AppendValue(out, value.members[i].value, depth + 1);
    }

    *out += '\n';
    out->append(depth * cIndentWidth, ' ');
    *out += '}';
}

static void AppendValue(std::string* out, const Value& value, u32 depth)
{
    switch (value.type)
    {
    case Type::Null:
        *out += "null";
        break;
    case Type::Bool:
        *out += value.boolean ? "true" : "false";
        break;
    case Type::Number:
        AppendNumber(out, value.number);
        break;
    case Type::String:
        AppendString(out, value.string.c_str());
        break;
    case Type::Array:
        AppendArray(out, value, depth);
        break;
    case Type::Object:
        AppendObject(out, value, depth);
        break;
    }
}

void Write(const Value& value, std::string* out)
{
    AppendValue(out, value, 0);
    *out += '\n';
}

const Value* Value::find(const char* key) const
{
    if (type != Type::Object)
        return nullptr;

    for (const Member& member : members)
    {
        if (member.key == key)
            return &member.value;
    }

    return nullptr;
}

const Value* Value::findObject(const char* key) const
{
    const Value* value = find(key);

    return value && value->isObject() ? value : nullptr;
}

const Value* Value::findArray(const char* key) const
{
    const Value* value = find(key);

    return value && value->isArray() ? value : nullptr;
}

const std::string* Value::findString(const char* key) const
{
    const Value* value = find(key);

    return value && value->isString() ? &value->string : nullptr;
}

bool Value::findInteger(const char* key, s64* out) const
{
    const Value* value = find(key);

    if (!value || value->type != Type::Number || std::floor(value->number) != value->number)
        return false;

    if (std::fabs(value->number) > cIntegerMax)
        return false;

    *out = static_cast<s64>(value->number);

    return true;
}

bool Value::getBool(const char* key, bool fallback) const
{
    const Value* value = find(key);

    return value && value->type == Type::Bool ? value->boolean : fallback;
}

s32 Value::getInt(const char* key, s32 fallback) const
{
    s64 number;

    if (!findInteger(key, &number) || number < INT32_MIN || number > INT32_MAX)
        return fallback;

    return static_cast<s32>(number);
}

f32 Value::getFloat(const char* key, f32 fallback) const
{
    const Value* value = find(key);

    return value && value->type == Type::Number && std::isfinite(value->number) ? static_cast<f32>(value->number) : fallback;
}

Value& Value::member(const char* key)
{
    type = Type::Object;

    for (Member& member : members)
    {
        if (member.key == key)
            return member.value;
    }

    members.push_back({ key, Value() });

    return members.back().value;
}

Value& Value::setArray(const char* key)
{
    Value& value = member(key);

    value = Value();
    value.type = Type::Array;

    return value;
}

void Value::setBool(const char* key, bool value)
{
    Value& target = member(key);

    target = Value();
    target.type = Type::Bool;
    target.boolean = value;
}

void Value::setInt(const char* key, s64 value)
{
    Value& target = member(key);

    target = Value();
    target.type = Type::Number;
    target.number = static_cast<f64>(value);
}

void Value::setFloat(const char* key, f32 value)
{
    Value& target = member(key);

    target = Value();
    target.type = Type::Number;
    target.number = RoundToSingleText(value);
}

void Value::setString(const char* key, const char* value)
{
    Value& target = member(key);

    target = Value();
    target.type = Type::String;
    target.string = value;
}

Value& Value::pushObject()
{
    type = Type::Array;

    items.emplace_back();
    items.back().type = Type::Object;

    return items.back();
}

void Value::pushFloat(f32 value)
{
    type = Type::Array;

    items.emplace_back();
    items.back().type = Type::Number;
    items.back().number = RoundToSingleText(value);
}

void Value::pushString(const char* value)
{
    type = Type::Array;

    items.emplace_back();
    items.back().type = Type::String;
    items.back().string = value;
}

}
