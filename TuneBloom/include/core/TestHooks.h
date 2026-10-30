#pragma once

#if __has_include(<local/TestHooks.h>)
#include <local/TestHooks.h>
#else
#include <basis/seadTypes.h>

class AudioResource;
class BarsListFile;
class Bfsar;
class LooseStreamFile;
class LooseWaveFile;
struct GLFWwindow;
struct ImDrawData;

namespace testhooks
{

inline bool IsEnabled()
{
    return false;
}

inline void HideWindow(GLFWwindow*)
{
}

inline void LoadSettings()
{
}

inline void Update()
{
}

inline void ApplyInputOverrides()
{
}

inline bool TakeBrowserRequest(const char*, bool)
{
    return false;
}

inline bool GetDropCursor(f64*, f64*)
{
    return false;
}

inline const char* GetOpenFilePath()
{
    return nullptr;
}

inline const char* GetSaveFilePath()
{
    return nullptr;
}

inline void Capture(ImDrawData*)
{
}

inline void CaptureLoadFrame(ImDrawData*, int)
{
}

inline bool IsMotionOn()
{
    return false;
}

inline bool IsViewportsOn()
{
    return false;
}

inline bool IsNativeMenuOn()
{
    return false;
}

inline bool IsBackupOn()
{
    return false;
}

inline s32 GetBackupSeconds()
{
    return 0;
}

inline u64 GetDocumentMemoryBudget()
{
    return 0;
}

inline void ReportLoadStage(const char*, u32, u32, f64)
{
}

inline void ReportLoadTotal(f64, s32, f64, f64)
{
}

inline const char* GetOpenAsPath()
{
    return nullptr;
}

inline void ApplyGroupFiles(const char*)
{
}

inline bool ApplyGroupCommands(Bfsar&)
{
    return true;
}

inline bool ApplyResourceCommands(AudioResource&)
{
    return true;
}

inline bool ApplyBarsListCommands(BarsListFile&)
{
    return true;
}

inline bool ApplyLooseFileCommands(LooseStreamFile&)
{
    return true;
}

inline bool ApplyLooseFileCommands(LooseWaveFile&)
{
    return true;
}

inline bool IsGroupFileStoreOn()
{
    return false;
}

inline bool IsStreamRewriteForced(const char*)
{
    return false;
}

inline bool IsOpusReencodeForced(const char*)
{
    return false;
}

}
#endif
