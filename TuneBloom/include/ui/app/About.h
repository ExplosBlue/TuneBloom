#pragma once

inline constexpr const char* cRepositoryUrl = "https://github.com/stupidestmodder/TuneBloom";
inline constexpr const char* cDiscordUrl    = "https://go.nsmbu.net/discord";
inline constexpr const char* cAuthorUrl     = "https://github.com/stupidestmodder";
inline constexpr const char* cIssuesUrl     = "https://github.com/stupidestmodder/TuneBloom/issues";

void RequestAbout();
void DrawAboutPopup();
void DrawAppIcon(float size);
const char* GetAppVersionFormat();
