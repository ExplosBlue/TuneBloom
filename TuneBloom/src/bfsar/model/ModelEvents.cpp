#include <bfsar/model/ModelEvents.h>

namespace modelevents
{

static Handlers sHandlers;
static u64 sLatestEditGeneration = 0;

void SetHandlers(const Handlers& handlers)
{
    sHandlers = handlers;
}

void ItemsRemoved(const std::vector<Item*>& items)
{
    if (sHandlers.itemsRemoved)
        sHandlers.itemsRemoved(items);
}

void ArchiveChanged()
{
    if (sHandlers.archiveChanged)
        sHandlers.archiveChanged();
}

void ErrorNotice(const char* title, const char* text)
{
    if (sHandlers.errorNotice)
        sHandlers.errorNotice(title, text);
}

void SetLoadStageCount(LoadStage stage, u32 count)
{
    if (sHandlers.loadStageCount)
        sHandlers.loadStageCount(stage, count);
}

void BeginLoadStage(LoadStage stage)
{
    if (sHandlers.loadStageBegin)
        sHandlers.loadStageBegin(stage);
}

void StepLoad()
{
    if (sHandlers.loadStep)
        sHandlers.loadStep();
}

void UpdateLoad()
{
    if (sHandlers.loadUpdate)
        sHandlers.loadUpdate();
}

bool LoadState(const char* section, json::Value* out)
{
    return sHandlers.loadState && sHandlers.loadState(section, out);
}

bool SaveState(const char* section, const json::Value& value)
{
    return sHandlers.saveState && sHandlers.saveState(section, value);
}

u64 NewEditGeneration()
{
    return ++sLatestEditGeneration;
}

u64 GetLatestEditGeneration()
{
    return sLatestEditGeneration;
}

}
