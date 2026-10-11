#pragma once

#include <framework/seadFramework.h>
#include <gfx/seadColor.h>
#include <gfx/seadTexture.h>

namespace util {

sead::Framework* getFramework();
void setFramework_(sead::Framework* framework);
void setClearColor(const sead::Color4f& color);

sead::Texture* getIcon();
void setIcon_(sead::Texture* icon);

inline const sead::SafeString cAppName("TuneBloom");

extern const sead::SafeString cAppVersion;
extern const bool cIsCommitBuild;

bool updateTitle(const char* path, bool dirty = false);
void refreshTitleDirty(bool dirty);

bool isAssertDialogOpen();

} // namespace util
