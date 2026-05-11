#pragma once

#include <framework/seadFramework.h>
#include <gfx/seadTexture.h>

namespace util {

sead::Framework* getFramework();
void setFramework_(sead::Framework* framework);

sead::Texture* getIcon();
void setIcon_(sead::Texture* icon);

#if defined(COMMIT_SHA)
inline const sead::SafeString cAppName("TuneBloom-" COMMIT_SHA);
inline const sead::SafeString cAppVersion(COMMIT_SHA);
#else
inline const sead::SafeString cAppName("TuneBloom");
inline const sead::SafeString cAppVersion("1.0");
#endif

} // namespace util
