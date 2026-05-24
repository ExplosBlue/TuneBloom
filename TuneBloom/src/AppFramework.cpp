#include "AppFramework.h"

AppFramework::AppFramework(const CreateArg& arg)
    : sead::GameFrameworkGlfwGL(arg)
{
}

void AppFramework::initialize(const InitializeArg& arg)
{
    sead::GameFrameworkGlfwGL::initialize(arg);
}
