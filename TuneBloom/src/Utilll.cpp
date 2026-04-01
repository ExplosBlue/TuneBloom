#include <Utilll.h>

#include <gfx/seadTexture.h>

static sead::Framework* sFramework = nullptr;
static sead::Texture* sIcon = nullptr;

namespace util {

sead::Framework* getFramework()
{
    return sFramework;
}

void setFramework_(sead::Framework* framework)
{
    sFramework = framework;
}

sead::Texture* getIcon()
{
    return sIcon;
}

void setIcon_(sead::Texture* icon)
{
    sIcon = icon;
}

} // namespace util
