#pragma once

#include <basis/seadTypes.h>

#include <optional>

struct ImGuiWindow;

namespace json
{
struct Value;
}

class FilesColumnWidth
{
public:
    void load(const json::Value& section);
    void save(json::Value* section) const;
    bool update(const ImGuiWindow* filesWindow);

private:
    f32& slotFor(bool isFiles);

    f32 mFilesWidthEm = 0.0f;
    f32 mProjectWidthEm = 0.0f;
    std::optional<bool> mWasFilesActive;
    f32 mPendingWidth = 0.0f;
    f32 mLastWidth = 0.0f;
    s32 mPendingFrames = 0;
};
