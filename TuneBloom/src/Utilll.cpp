#include <Utilll.h>

#include <framework/glfw/seadGameFrameworkGlfwGL.h>
#include <gfx/seadTexture.h>

static sead::Framework* sFramework = nullptr;
static sead::Texture* sIcon = nullptr;
static sead::FixedSafeString<512> sLastTitlePath;

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

bool updateTitle(const char* path, bool dirty)
{
    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(getFramework());
    if (!fw)
    {
        return false;
    }

    if (path)
    {
        sLastTitlePath = path;
    }
    else
    {
        sLastTitlePath.clear();
    }

    sead::FixedSafeString<512> title;
    title.format("%s %s", cAppName.cstr(), cAppVersion.cstr());

    if (path)
    {
        title.appendWithFormat(" - %s%s", dirty ? "*" : "", path);
    }

    fw->setCaption(title);
    return true;
}

void refreshTitleDirty(bool dirty)
{
    updateTitle(sLastTitlePath.isEmpty() ? nullptr : sLastTitlePath.cstr(), dirty);
}

} // namespace util
