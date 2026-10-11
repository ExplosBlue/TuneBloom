#include <ui/items/ItemColumnSettings.h>

#include <ui/items/ItemColumns.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <cstdio>
#include <cstring>

static constexpr u32 cItemColumnTitleMax = 24;
static constexpr u32 cItemColumnKindMax  = 32;

struct ItemColumnUserWidth
{
    char title[cItemColumnTitleMax];
    f32 em;
    bool hidden;
};

static ItemColumnUserWidth sUserWidths[cItemColumnKindMax];
static u32 sUserWidthNum = 0;

static ItemColumnUserWidth* FindItemColumnUserWidth(const char* title, bool add)
{
    for (u32 i = 0; i < sUserWidthNum; i++)
        if (strcmp(sUserWidths[i].title, title) == 0)
            return &sUserWidths[i];

    if (!add || sUserWidthNum >= cItemColumnKindMax)
        return nullptr;

    ItemColumnUserWidth& entry = sUserWidths[sUserWidthNum++];
    ImStrncpy(entry.title, title, cItemColumnTitleMax);
    entry.em = 0.0f;
    entry.hidden = false;

    return &entry;
}

f32 GetItemColumnUserWidth(const char* key)
{
    const ItemColumnUserWidth* entry = FindItemColumnUserWidth(key, false);

    return entry ? entry->em : 0.0f;
}

void SetItemColumnUserWidth(const char* key, f32 em)
{
    if (ItemColumnUserWidth* entry = FindItemColumnUserWidth(key, true))
        entry->em = em;
}

void ApplyItemColumnHidden(ImGuiTableColumn& column, const char* key)
{
    if (column.Flags & ImGuiTableColumnFlags_DefaultHide)
        return;

    const ItemColumnUserWidth* entry = FindItemColumnUserWidth(key, false);

    column.IsUserEnabledNextFrame = !(entry && entry->hidden);
}

void StoreItemColumnHidden(const ImGuiTableColumn& column, const char* key)
{
    if (column.IsUserEnabledNextFrame == column.IsUserEnabled || (column.Flags & ImGuiTableColumnFlags_DefaultHide))
        return;

    if (ItemColumnUserWidth* entry = FindItemColumnUserWidth(key, true))
        entry->hidden = !column.IsUserEnabledNextFrame;

    ImGui::MarkIniSettingsDirty();
}

static void* ItemColumnSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return sUserWidths;
}

static void ItemColumnSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    f32 em = 0.0f;
    char title[cItemColumnTitleMax] = {};

    if (sscanf(line, "Hidden=%23[^\n]", title) == 1)
    {
        if (ItemColumnUserWidth* entry = FindItemColumnUserWidth(title, true))
            entry->hidden = true;

        return;
    }

    if (sscanf(line, "Width=%f,%23[^\n]", &em, title) != 2 || em <= 0.0f)
        return;

    if (ItemColumnUserWidth* entry = FindItemColumnUserWidth(title, true))
        entry->em = em;
}

static void ItemColumnSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    buf->appendf("[%s][Widths]\n", handler->TypeName);

    for (u32 i = 0; i < sUserWidthNum; i++)
        if (sUserWidths[i].em > 0.0f)
            buf->appendf("Width=%.3f,%s\n", sUserWidths[i].em, sUserWidths[i].title);

    for (u32 i = 0; i < sUserWidthNum; i++)
        if (sUserWidths[i].hidden)
            buf->appendf("Hidden=%s\n", sUserWidths[i].title);

    buf->append("\n");
}

void RegisterItemColumnSettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "ItemColumns";
    handler.TypeHash = ImHashStr("ItemColumns");
    handler.ReadOpenFn = ItemColumnSettingsReadOpen;
    handler.ReadLineFn = ItemColumnSettingsReadLine;
    handler.WriteAllFn = ItemColumnSettingsWriteAll;

    ImGui::AddSettingsHandler(&handler);
}
