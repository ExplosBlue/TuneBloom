#pragma once

#include <framework/glfw/seadGameFrameworkGlfwGL.h>

class AppFramework : public sead::GameFrameworkGlfwGL
{
    SEAD_RTTI_OVERRIDE(AppFramework, sead::GameFrameworkGlfwGL);

public:
    explicit AppFramework(const CreateArg& arg);

    static void initialize(const InitializeArg& arg);
};
