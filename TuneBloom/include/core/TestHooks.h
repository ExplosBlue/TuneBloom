#pragma once

#if __has_include(<local/TestHooks.h>)
#include <local/TestHooks.h>
#else
#include <basis/seadTypes.h>

struct GLFWwindow;
struct ImDrawData;

namespace testhooks
{

inline bool IsEnabled() { return false; }
inline void HideWindow(GLFWwindow*) {}
inline void LoadSettings() {}
inline void Update() {}
inline bool TakeBrowserRequest(const char*, bool) { return false; }
inline const char* GetOpenFilePath() { return nullptr; }
inline void Capture(ImDrawData*) {}
inline void CaptureLoadFrame(ImDrawData*, int) {}
inline bool IsMotionOn() { return false; }
inline bool IsBackupOn() { return false; }
inline s32 GetBackupSeconds() { return 0; }
inline void ReportLoadStage(const char*, u32, u32, f64) {}
inline void ReportLoadTotal(f64, s32, f64, f64) {}

inline const char* GetOpenAsPath() { return nullptr; }
inline void ApplyGroupFiles(const char*) {}
inline bool ApplyGroupCommands() { return true; }
inline bool IsGroupFileStoreOn() { return false; }
inline bool IsStreamRewriteForced(const char*) { return false; }
inline bool IsOpusReencodeForced(const char*) { return false; }

}
#endif
