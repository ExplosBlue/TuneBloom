#pragma once

#include <cstdio>
#include <cstdlib>

inline bool& gDebugEnabled()
{
    static bool enabled = []{
        const char* env = std::getenv("TUNEBLOOM_DEBUG");
        return env && env[0] != '\0';
    }();
    return enabled;
}

inline void EnableDebug() { gDebugEnabled() = true; }
inline void DisableDebug() { gDebugEnabled() = false; }

#define LOG(...) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] " __VA_ARGS__); } while(0)
#define LOG_FUNC() do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s()\n", __FILE__, __LINE__, __func__); } while(0)
#define LOG_FMT(fmt, ...) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while(0)
#define LOG_ENTER() do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] >>> %s:%d %s()\n", __FILE__, __LINE__, __func__); } while(0)
#define LOG_EXIT() do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] <<< %s:%d %s()\n", __FILE__, __LINE__, __func__); } while(0)
#define LOG_VALUE(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %s\n", __FILE__, __LINE__, __func__, name, value); } while(0)
#define LOG_U32(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %u (0x%08X)\n", __FILE__, __LINE__, __func__, name, (unsigned)(value), (unsigned)(value)); } while(0)
#define LOG_U16(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %u (0x%04X)\n", __FILE__, __LINE__, __func__, name, (unsigned)(value), (unsigned)(value)); } while(0)
#define LOG_U64(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %llu (0x%016llX)\n", __FILE__, __LINE__, __func__, name, (unsigned long long)(value), (unsigned long long)(value)); } while(0)
#define LOG_S32(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %d (0x%08X)\n", __FILE__, __LINE__, __func__, name, (int)(value), (unsigned)(value)); } while(0)
#define LOG_BOOL(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %s\n", __FILE__, __LINE__, __func__, name, (value) ? "true" : "false"); } while(0)
#define LOG_PTR(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %p\n", __FILE__, __LINE__, __func__, name, (const void*)(value)); } while(0)
#define LOG_STR(str) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - \"%s\"\n", __FILE__, __LINE__, __func__, (str)); } while(0)
#define LOG_HEX(name, value, len) do { if (gDebugEnabled()) { fprintf(stderr, "[TuneBloom] %s:%d %s() - %s (%u bytes): ", __FILE__, __LINE__, __func__, name, (unsigned)(len)); for (u32 _i = 0; _i < (len) && _i < 32; _i++) fprintf(stderr, "%02X ", ((const u8*)(value))[_i]); fprintf(stderr, "\n"); } } while(0)
#define LOG_SIZE(name, value) do { if (gDebugEnabled()) fprintf(stderr, "[TuneBloom] %s:%d %s() - %s = %zu\n", __FILE__, __LINE__, __func__, name, (size_t)(value)); } while(0)
