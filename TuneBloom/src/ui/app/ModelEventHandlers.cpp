#include <ui/app/ModelEventHandlers.h>

#include <ui/settings/ConfigFile.h>
#include <ui/file/LoadProgress.h>
#include <bfsar/model/ModelEvents.h>
#include <ui/app/Notifications.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>

static void MarkArchiveChanged()
{
    SetUnsavedChanges(true);
}

static void AddErrorNotice(const char* title, const char* text)
{
    notify::Add(notify::Kind::Error, title, text);
}

static bool LoadStateSection(const char* section, json::Value* out)
{
    return LoadConfigSection(cStateFile, section, out);
}

static bool SaveStateSection(const char* section, const json::Value& value)
{
    return SaveConfigSection(cStateFile, section, value);
}

void InstallModelEventHandlers()
{
    modelevents::Handlers handlers;
    handlers.itemsRemoved = &ForgetRemovedItems;
    handlers.archiveChanged = &MarkArchiveChanged;
    handlers.errorNotice = &AddErrorNotice;
    handlers.loadStageCount = &loadprogress::SetStageCount;
    handlers.loadStageBegin = &loadprogress::BeginStage;
    handlers.loadStep = &loadprogress::Step;
    handlers.loadUpdate = &loadprogress::Update;
    handlers.loadState = &LoadStateSection;
    handlers.saveState = &SaveStateSection;

    modelevents::SetHandlers(handlers);
}
