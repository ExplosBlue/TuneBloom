#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace json
{

enum class Type
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

struct Member;

struct Value
{
    Type type = Type::Null;
    bool boolean = false;
    f64 number = 0.0;
    std::string string;
    std::vector<Value> items;
    std::vector<Member> members;

    bool isObject() const
    {
        return type == Type::Object;
    }

    bool isArray() const
    {
        return type == Type::Array;
    }

    bool isString() const
    {
        return type == Type::String;
    }

    const Value* find(const char* key) const;
    const Value* findObject(const char* key) const;
    const Value* findArray(const char* key) const;
    const std::string* findString(const char* key) const;
    bool findInteger(const char* key, s64* out) const;

    bool getBool(const char* key, bool fallback) const;
    s32 getInt(const char* key, s32 fallback) const;
    f32 getFloat(const char* key, f32 fallback) const;

    Value& member(const char* key);
    Value& setArray(const char* key);
    void setBool(const char* key, bool value);
    void setInt(const char* key, s64 value);
    void setFloat(const char* key, f32 value);
    void setString(const char* key, const char* value);

    Value& pushObject();
    void pushFloat(f32 value);
    void pushString(const char* value);
};

struct Member
{
    std::string key;
    Value value;
};

bool Parse(const char* text, size_t size, Value* out);
void Write(const Value& value, std::string* out);

void AppendString(std::string* out, const char* text);

}
