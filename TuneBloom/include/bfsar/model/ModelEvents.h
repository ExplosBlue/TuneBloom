#pragma once

#include <basis/seadTypes.h>

#include <vector>

class Item;

namespace json
{
struct Value;
}

namespace modelevents
{

enum class LoadStage
{
    ReadFile,
    Groups,
    SequenceFiles,
    WaveFiles,
    BankFiles,
    Sounds,
    SoundSets,
    GroupFiles,
    Names,
    Num
};

struct Handlers
{
    void (*itemsRemoved)(const std::vector<Item*>& items) = nullptr;
    void (*archiveChanged)() = nullptr;
    void (*errorNotice)(const char* title, const char* text) = nullptr;
    void (*loadStageCount)(LoadStage stage, u32 count) = nullptr;
    void (*loadStageBegin)(LoadStage stage) = nullptr;
    void (*loadStep)() = nullptr;
    void (*loadUpdate)() = nullptr;
    bool (*loadState)(const char* section, json::Value* out) = nullptr;
    bool (*saveState)(const char* section, const json::Value& value) = nullptr;
};

void SetHandlers(const Handlers& handlers);

void ItemsRemoved(const std::vector<Item*>& items);
void ArchiveChanged();
void ErrorNotice(const char* title, const char* text);

void SetLoadStageCount(LoadStage stage, u32 count);
void BeginLoadStage(LoadStage stage);
void StepLoad();
void UpdateLoad();

bool LoadState(const char* section, json::Value* out);
bool SaveState(const char* section, const json::Value& value);

u64 NewEditGeneration();
u64 GetLatestEditGeneration();

}
