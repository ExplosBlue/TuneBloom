#import <Cocoa/Cocoa.h>
#include <platform/NativeMenu.h>

#include <core/MenuTree.h>
#include <imgui/imgui.h>

@interface TuneBloomMenu : NSMenu
@property (nonatomic) u32 menuId;
@end

@implementation TuneBloomMenu

- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    return NO;
}

@end

@interface TuneBloomMenuController : NSObject <NSMenuDelegate>
- (void)performItem:(NSMenuItem*)item;
@end

namespace nativemenu
{

static void (*sRecord)(MenuTree* tree) = nullptr;
static void (*sActivate)(u32 id) = nullptr;
static TuneBloomMenuController* sController = nil;
static NSMenuItem* sAppMenuItem = nil;
static NSMenuItem* sAboutItem = nil;
static NSMenuItem* sSettingsItem = nil;
static NSMenuItem* sWindowMenuItem = nil;
static bool sIsTracking = false;
static bool sIsRefreshPending = false;

static NSString* ToString(const char* text)
{
    NSString* string = text ? [NSString stringWithUTF8String:text] : nil;

    return string ? string : @"";
}

static unichar GetKeyCharacter(ImGuiKey key)
{
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
        return static_cast<unichar>('a' + (key - ImGuiKey_A));

    if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
        return static_cast<unichar>('0' + (key - ImGuiKey_0));

    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12)
        return static_cast<unichar>(NSF1FunctionKey + (key - ImGuiKey_F1));

    switch (key)
    {
        case ImGuiKey_Delete:
            return NSDeleteFunctionKey;
        case ImGuiKey_Backspace:
            return NSBackspaceCharacter;
        case ImGuiKey_Enter:
            return NSCarriageReturnCharacter;
        case ImGuiKey_Escape:
            return 0x1B;
        case ImGuiKey_Space:
            return ' ';
        case ImGuiKey_Comma:
            return ',';
        case ImGuiKey_Period:
            return '.';
        case ImGuiKey_Minus:
            return '-';
        case ImGuiKey_Equal:
            return '=';
        case ImGuiKey_Slash:
            return '/';
        case ImGuiKey_UpArrow:
            return NSUpArrowFunctionKey;
        case ImGuiKey_DownArrow:
            return NSDownArrowFunctionKey;
        case ImGuiKey_LeftArrow:
            return NSLeftArrowFunctionKey;
        case ImGuiKey_RightArrow:
            return NSRightArrowFunctionKey;
        case ImGuiKey_Home:
            return NSHomeFunctionKey;
        case ImGuiKey_End:
            return NSEndFunctionKey;
        case ImGuiKey_PageUp:
            return NSPageUpFunctionKey;
        case ImGuiKey_PageDown:
            return NSPageDownFunctionKey;
        default:
            return 0;
    }
}

static NSEventModifierFlags GetKeyModifiers(s32 chord)
{
    NSEventModifierFlags modifiers = 0;

    if (chord & (ImGuiMod_Shortcut | ImGuiMod_Super))
        modifiers |= NSEventModifierFlagCommand;

    if (chord & ImGuiMod_Ctrl)
        modifiers |= NSEventModifierFlagControl;

    if (chord & ImGuiMod_Alt)
        modifiers |= NSEventModifierFlagOption;

    if (chord & ImGuiMod_Shift)
        modifiers |= NSEventModifierFlagShift;

    return modifiers;
}

static void SetKeyEquivalent(NSMenuItem* item, s32 chord)
{
    const unichar character = chord != 0 ? GetKeyCharacter(static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_)) : 0;

    if (character == 0)
        return;

    item.keyEquivalent = [NSString stringWithCharacters:&character length:1];
    item.keyEquivalentModifierMask = GetKeyModifiers(chord);
}

static bool IsMovedRole(MenuRole role)
{
    return role == MenuRole::About || role == MenuRole::Quit;
}

static s32 FindNode(const MenuTree& tree, u32 id)
{
    for (size_t i = 0; i < tree.nodes.size(); i++)
    {
        if (tree.nodes[i].id == id)
            return static_cast<s32>(i);
    }

    return -1;
}

static s32 FindRole(const MenuTree& tree, MenuRole role)
{
    for (size_t i = 0; i < tree.nodes.size(); i++)
    {
        if (tree.nodes[i].role == role)
            return static_cast<s32>(i);
    }

    return -1;
}

static TuneBloomMenu* MakeMenu(NSString* title, u32 menuId)
{
    TuneBloomMenu* menu = [[[TuneBloomMenu alloc] initWithTitle:title] autorelease];
    menu.menuId = menuId;
    menu.autoenablesItems = NO;

    return menu;
}

static void AddItems(NSMenu* menu, const MenuTree& tree, s32 parent);

static NSMenuItem* MakeItem(const MenuTree& tree, s32 index)
{
    const MenuNode& node = tree.nodes[index];
    NSMenuItem* item = [[[NSMenuItem alloc] initWithTitle:ToString(node.label.c_str()) action:nil keyEquivalent:@""] autorelease];

    item.enabled = node.enabled;

    if (!node.tooltip.empty())
        item.toolTip = ToString(node.tooltip.c_str());

    if (node.kind == MenuNodeKind::Menu)
    {
        TuneBloomMenu* submenu = MakeMenu(item.title, 0);
        AddItems(submenu, tree, index);
        item.submenu = submenu;
        return item;
    }

    SetKeyEquivalent(item, node.keyChord);
    item.state = node.checked ? NSControlStateValueOn : NSControlStateValueOff;
    item.tag = static_cast<NSInteger>(node.id);
    item.target = sController;
    item.action = @selector(performItem:);

    return item;
}

static void AddItems(NSMenu* menu, const MenuTree& tree, s32 parent)
{
    bool hasSeparator = false;

    for (size_t i = 0; i < tree.nodes.size(); i++)
    {
        const MenuNode& node = tree.nodes[i];

        if (node.parent != parent || IsMovedRole(node.role))
            continue;

        if (node.kind == MenuNodeKind::Separator)
        {
            hasSeparator = menu.numberOfItems > 0;
            continue;
        }

        if (hasSeparator)
            [menu addItem:[NSMenuItem separatorItem]];

        hasSeparator = false;
        [menu addItem:MakeItem(tree, static_cast<s32>(i))];
    }
}

static void UpdateAppItem(NSMenuItem* item, const MenuTree& tree, MenuRole role)
{
    const s32 index = FindRole(tree, role);

    item.hidden = index < 0;

    if (index < 0)
        return;

    item.tag = static_cast<NSInteger>(tree.nodes[index].id);
    item.enabled = tree.nodes[index].enabled;
}

static NSMenuItem* AddAppItem(NSMenu* menu, const char* title, SEL action, NSString* key, NSEventModifierFlags modifiers)
{
    NSMenuItem* item = [menu addItemWithTitle:ToString(title) action:action keyEquivalent:key];
    item.keyEquivalentModifierMask = modifiers;

    return item;
}

static NSMenuItem* MakeAppMenuItem(const Texts& texts)
{
    NSMenu* menu = [[[NSMenu alloc] initWithTitle:ToString(texts.appName)] autorelease];
    menu.autoenablesItems = NO;

    sAboutItem = [AddAppItem(menu, texts.about, @selector(performItem:), @"", 0) retain];
    sAboutItem.target = sController;
    [menu addItem:[NSMenuItem separatorItem]];

    sSettingsItem = [AddAppItem(menu, texts.settings, @selector(performItem:), @",", NSEventModifierFlagCommand) retain];
    sSettingsItem.target = sController;
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenu* servicesMenu = [[[NSMenu alloc] initWithTitle:ToString(texts.services)] autorelease];
    [menu addItemWithTitle:ToString(texts.services) action:nil keyEquivalent:@""].submenu = servicesMenu;
    [NSApp setServicesMenu:servicesMenu];
    [menu addItem:[NSMenuItem separatorItem]];

    AddAppItem(menu, texts.hide, @selector(hide:), @"h", NSEventModifierFlagCommand);
    AddAppItem(menu, texts.hideOthers, @selector(hideOtherApplications:), @"h", NSEventModifierFlagCommand | NSEventModifierFlagOption);
    AddAppItem(menu, texts.showAll, @selector(unhideAllApplications:), @"", 0);
    [menu addItem:[NSMenuItem separatorItem]];

    AddAppItem(menu, texts.quit, @selector(terminate:), @"q", NSEventModifierFlagCommand);

    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:ToString(texts.appName) action:nil keyEquivalent:@""];
    item.submenu = menu;

    return item;
}

static NSMenuItem* MakeWindowMenuItem(const Texts& texts)
{
    NSMenu* menu = [[[NSMenu alloc] initWithTitle:ToString(texts.window)] autorelease];

    AddAppItem(menu, texts.minimize, @selector(performMiniaturize:), @"m", NSEventModifierFlagCommand);
    AddAppItem(menu, texts.zoom, @selector(performZoom:), @"", 0);
    [menu addItem:[NSMenuItem separatorItem]];
    AddAppItem(menu, texts.bringAllToFront, @selector(arrangeInFront:), @"", 0);

    [NSApp setWindowsMenu:menu];

    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:menu.title action:nil keyEquivalent:@""];
    item.submenu = menu;

    return item;
}

static void RebuildMainMenu(const MenuTree& tree)
{
    NSMenu* mainMenu = [NSApp mainMenu];
    NSMenu* helpMenu = nil;

    while (mainMenu.numberOfItems > 1)
        [mainMenu removeItemAtIndex:1];

    for (size_t i = 0; i < tree.nodes.size(); i++)
    {
        const MenuNode& node = tree.nodes[i];

        if (node.parent != -1 || node.kind != MenuNodeKind::Menu || node.role == MenuRole::Settings)
            continue;

        NSString* title = ToString(node.label.c_str());
        TuneBloomMenu* menu = MakeMenu(title, node.id);
        menu.delegate = sController;
        AddItems(menu, tree, static_cast<s32>(i));

        NSMenuItem* item = [[[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""] autorelease];
        item.submenu = menu;

        if (node.role == MenuRole::Help)
        {
            [mainMenu addItem:sWindowMenuItem];
            helpMenu = menu;
        }

        [mainMenu addItem:item];
    }

    if (!helpMenu)
        [mainMenu addItem:sWindowMenuItem];

    [NSApp setHelpMenu:helpMenu];
    UpdateAppItem(sAboutItem, tree, MenuRole::About);
    UpdateAppItem(sSettingsItem, tree, MenuRole::Settings);
}

static void RebuildMenu(NSMenu* menu)
{
    if (![menu isKindOfClass:[TuneBloomMenu class]] || !sRecord)
        return;

    MenuTree tree;
    sRecord(&tree);

    const s32 index = FindNode(tree, static_cast<TuneBloomMenu*>(menu).menuId);

    [menu removeAllItems];

    if (index >= 0)
        AddItems(menu, tree, index);

    UpdateAppItem(sAboutItem, tree, MenuRole::About);
    UpdateAppItem(sSettingsItem, tree, MenuRole::Settings);
}

static void ActivateItem(NSMenuItem* item)
{
    if (sActivate)
        sActivate(static_cast<u32>(item.tag));
}

bool IsSupported()
{
    return true;
}

bool IsInstalled()
{
    return sController != nil;
}

static void ObserveTracking()
{
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

    [center addObserverForName:NSMenuDidBeginTrackingNotification object:nil queue:nil usingBlock:^(NSNotification*)
    {
        sIsTracking = true;
    }];

    [center addObserverForName:NSMenuDidEndTrackingNotification object:nil queue:nil usingBlock:^(NSNotification*)
    {
        sIsTracking = false;

        if (sIsRefreshPending)
            Refresh();
    }];
}

void Install(const Setup& setup)
{
    if (sController || !setup.record || !setup.activate || ![NSThread isMainThread])
        return;

    sRecord = setup.record;
    sActivate = setup.activate;
    sController = [[TuneBloomMenuController alloc] init];
    sAppMenuItem = MakeAppMenuItem(setup.texts);
    sWindowMenuItem = MakeWindowMenuItem(setup.texts);

    NSMenu* mainMenu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [mainMenu addItem:sAppMenuItem];
    [NSApp setMainMenu:mainMenu];

    ObserveTracking();
}

void Refresh()
{
    if (!sController || !sRecord)
        return;

    sIsRefreshPending = sIsTracking;

    if (sIsTracking)
        return;

    MenuTree tree;
    sRecord(&tree);
    RebuildMainMenu(tree);
}

}

@implementation TuneBloomMenuController

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    nativemenu::RebuildMenu(menu);
}

- (BOOL)menuHasKeyEquivalent:(NSMenu*)menu forEvent:(NSEvent*)event target:(id*)target action:(SEL*)action
{
    return NO;
}

- (void)performItem:(NSMenuItem*)item
{
    nativemenu::ActivateItem(item);
}

@end
