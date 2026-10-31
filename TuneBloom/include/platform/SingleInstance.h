#pragma once

struct GLFWwindow;

namespace singleinstance
{

inline constexpr const char* cNewWindowArgument = "--new-window";

enum class Target
{
    OpenDocument,
    Player,
    Editor,
};

using TakeFunc = bool (*)(const char* path, Target target);

bool IsSupported();
bool ForwardToRunningApp(const char* path);
void StartListening(GLFWwindow* window, TakeFunc take);

}
