#include <ui/file/DocumentMgr.h>

#include <ui/backup/Backup.h>
#include <ui/settings/Config.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentState.h>
#include <ui/export/Export.h>
#include <ui/file/FileFlow.h>
#include <ui/file/FileWindow.h>
#include <ui/file/LoadProgress.h>
#include <ui/Messages.h>
#include <ui/player/PlayerFrame.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/file/RecentFiles.h>
#include <core/TestHooks.h>
#include <ui/file/UnsavedChanges.h>

#include <bfsar/io/FileIo.h>
#include <bfsar/player/SoundPlayer.h>
#include <snd-ply/snd/SoundThread.h>

#include <imgui/imgui_internal.h>

#include <app/Utilll.h>

#include <algorithm>
#include <unordered_map>

static constexpr u64 cDocumentMemoryBudget = 1024ull * 1024ull * 1024ull;

static std::vector<Document*> sDocuments;
static Document* sActive = nullptr;
static bool sHasFocus = false;
static Document* sFocus = nullptr;
static u64 sLastActivation = 0;
static Document* sPendingActivation = nullptr;

static DocumentState& GetNoDocumentState()
{
    static DocumentState sState;
    return sState;
}

Document* ActiveDocument()
{
    return sHasFocus ? sFocus : sActive;
}

DocumentState& ActiveDocumentState()
{
    Document* document = ActiveDocument();

    return document ? document->state() : GetNoDocumentState();
}

const std::vector<Document*>& GetOpenDocuments()
{
    return sDocuments;
}

std::string CanonicalDocumentPath(const char* path)
{
    return fileio::CanonicalPath(path);
}

Document* FindOpenDocument(const char* path)
{
    if (!path || !path[0])
        return nullptr;

    const std::string canonical = CanonicalDocumentPath(path);

    for (Document* document : sDocuments)
    {
        if (document->state().canonicalPath == canonical)
            return document;
    }

    return nullptr;
}

Document* FindPreviewDocument()
{
    for (Document* document : sDocuments)
    {
        if (document->state().isPreview)
            return document;
    }

    return nullptr;
}

bool HasUnsavedDocuments()
{
    for (const Document* document : sDocuments)
    {
        if (document->state().unsaved)
            return true;
    }

    return false;
}

static bool ChangesParseContext(const Document* document)
{
    return document && document->isLoaded() && !(document->parseContext() == Document::CurrentParseContext());
}

static void LockSoundThread()
{
    if (snd::internal::driver::SoundThread::instance())
        snd::internal::driver::SoundThread::instance()->lock();
}

static void UnlockSoundThread()
{
    if (snd::internal::driver::SoundThread::instance())
        snd::internal::driver::SoundThread::instance()->unlock();
}

void PrepareParseEndian(sead::Endian::Types endian)
{
    if (endian != Document::CurrentParseContext().endian && sSoundPlayer.hasPlaybackOwner())
        sSoundPlayer.reset();
}

static void SwitchToParseContextOf(const Document* document)
{
    if (!ChangesParseContext(document))
        return;

    const Document::ParseContext context = document->parseContext();

    if (context.endian != Document::CurrentParseContext().endian && sSoundPlayer.hasPlaybackOwner() && !sSoundPlayer.isPlayingFrom(document))
        sSoundPlayer.reset();

    Document::ApplyParseContext(context);
}

DocumentFocus::DocumentFocus(Document* document)
    : mHadFocus(sHasFocus)
    , mPrevious(sFocus)
    , mLockedSound(false)
{
    const Document* current = ActiveDocument();

    sHasFocus = true;
    sFocus = document;

    if (document == current || !ChangesParseContext(document))
        return;

    LockSoundThread();
    mLockedSound = true;

    Document::ApplyParseContext(document->parseContext());
}

DocumentFocus::~DocumentFocus()
{
    sHasFocus = mHadFocus;
    sFocus = mPrevious;

    const Document* restored = ActiveDocument();

    if (ChangesParseContext(restored))
    {
        if (!mLockedSound)
            LockSoundThread();

        Document::ApplyParseContext(restored->parseContext());
        UnlockSoundThread();
    }
    else if (mLockedSound)
    {
        UnlockSoundThread();
    }
}

static void ReleasePlayback(Document& document)
{
    if (sSoundPlayer.isPlayingFrom(&document))
    {
        sSoundPlayer.reset();
        return;
    }

    const Sound* lastPlayed = sSoundPlayer.getLastPlayedSound();

    if (lastPlayed && document.isLoaded() && document.ownsItem(*lastPlayed))
        sSoundPlayer.resetLastPlayedSound();
}

static u64 GetMemoryBudget()
{
    const u64 testBudget = testhooks::GetDocumentMemoryBudget();

    return testBudget != 0 ? testBudget : cDocumentMemoryBudget;
}

static std::vector<std::vector<Item*>> CollectItemLists(Document& document)
{
    std::vector<std::vector<Item*>> lists;

    document.forEachItemList([&lists](Item::List& list)
    {
        std::vector<Item*>& items = lists.emplace_back();

        for (Item* item : list)
            items.push_back(item);
    });

    return lists;
}

static void CaptureUnloadedLayout(Document& document)
{
    using ItemLocator = DocumentState::ItemLocator;

    std::unordered_map<const Item*, ItemLocator> locators;
    const std::vector<std::vector<Item*>> lists = CollectItemLists(document);

    for (size_t list = 0; list < lists.size(); list++)
    {
        for (size_t index = 0; index < lists[list].size(); index++)
            locators[lists[list][index]] = { static_cast<s32>(list), static_cast<s32>(index) };
    }

    const auto locate = [&locators](const Item* item)
    {
        const auto found = locators.find(item);

        return found != locators.end() ? found->second : ItemLocator();
    };

    DocumentState& state = document.state();
    DocumentState::UnloadedLayout& layout = state.unloadedLayout;

    for (size_t tab = 0; tab < DocumentState::cTabCount; tab++)
    {
        const SelectionState& selection = state.selections[tab];
        DocumentState::TabLocators& locatorsOfTab = layout.tabs[tab];

        locatorsOfTab.selected = locate(selection.selectedItem);
        locatorsOfTab.anchor = locate(selection.multiSelectAnchor);
        locatorsOfTab.multiSelected.clear();

        for (const Item* item : selection.multiSelectedItems)
            locatorsOfTab.multiSelected.push_back(locate(item));
    }

    layout.fileWindows.clear();

    for (const Item* item : state.fileWindows)
    {
        const FileWindow* window = static_cast<const FileWindow*>(item);

        if (window->isOpen())
            layout.fileWindows.push_back(locate(window->getFileRef().getItem()));
    }

    const FileWindow* shown = state.shownFileWindow;

    layout.shownFile = shown && shown->isOpen() ? locate(shown->getFileRef().getItem()) : ItemLocator();
}

static void RestoreUnloadedLayout(Document& document)
{
    using ItemLocator = DocumentState::ItemLocator;

    const std::vector<std::vector<Item*>> lists = CollectItemLists(document);

    const auto resolve = [&lists](const ItemLocator& locator) -> Item*
    {
        if (locator.list < 0 || locator.list >= static_cast<s32>(lists.size()))
            return nullptr;

        const std::vector<Item*>& list = lists[locator.list];

        return locator.index >= 0 && locator.index < static_cast<s32>(list.size()) ? list[locator.index] : nullptr;
    };

    DocumentState& state = document.state();
    const DocumentState::UnloadedLayout& layout = state.unloadedLayout;

    for (size_t tab = 0; tab < DocumentState::cTabCount; tab++)
    {
        SelectionState& selection = state.selections[tab];
        const DocumentState::TabLocators& locatorsOfTab = layout.tabs[tab];

        selection = SelectionState();
        selection.selectedItem = resolve(locatorsOfTab.selected);
        selection.multiSelectAnchor = resolve(locatorsOfTab.anchor);

        for (const ItemLocator& locator : locatorsOfTab.multiSelected)
        {
            if (Item* item = resolve(locator))
                selection.multiSelectedItems.push_back(item);
        }
    }

    DocumentFocus focus(&document);

    for (const ItemLocator& locator : layout.fileWindows)
    {
        if (Item* item = resolve(locator))
            OpenFileWindow(item);
    }

    if (Item* shownFile = resolve(layout.shownFile))
        OpenFileWindow(shownFile);
    else
        ShowFileWindow(nullptr);

    state.unloadedLayout = DocumentState::UnloadedLayout();
}

static void UnloadDocument(Document& document)
{
    CaptureUnloadedLayout(document);

    PopupMgr::instance()->forgetAllItems();
    ReleasePlayback(document);

    DocumentState& state = document.state();

    for (SelectionState& selection : state.selections)
        selection = SelectionState();

    state.scrollItem = nullptr;
    state.itemErrors = PopupMgr::ItemErrors();
    state.savedItems.clear();
    state.memoryEstimate = 0;

    document.unload();
}

static bool ReloadDocument(Document& document)
{
    const std::string name = document.displayName();

    loadprogress::Begin(name.c_str());

    bool reloaded = false;

    {
        DocumentFocus focus(nullptr);
        reloaded = document.reload();
    }

    PopupMgr::instance()->discardProcessedErrors();
    loadprogress::End();

    if (!reloaded)
        return false;

    DocumentState& state = document.state();
    state.editGeneration = NewEditGeneration();
    state.memoryEstimate = document.estimateMemory();

    RestoreUnloadedLayout(document);

    {
        DocumentFocus focus(&document);
        CaptureSavedState();
    }

    return true;
}

static Document* FindMostRecentDocument(bool isLoaded)
{
    Document* recent = nullptr;

    for (Document* document : sDocuments)
    {
        if (document->isLoaded() != isLoaded || document->state().reloadFailed)
            continue;

        if (!recent || document->state().lastActive > recent->state().lastActive)
            recent = document;
    }

    return recent;
}

static void RemoveDocument(Document* document)
{
    const auto found = std::find(sDocuments.begin(), sDocuments.end(), document);

    if (found == sDocuments.end())
        return;

    if (document == sActive)
    {
        CancelPendingExportsAndImports();
        PopupMgr::instance()->closeFile();
    }

    PopupMgr::instance()->forgetAllItems();
    ForgetDocumentRequests(document);
    ForgetPlayerFrameDocument(document);
    ReleasePlayback(*document);

    if (document->isLoaded())
    {
        DocumentFocus focus(document);
        backup::OnDocumentClosed();
    }

    sDocuments.erase(found);

    if (document == sActive)
        sActive = nullptr;

    if (document == sPendingActivation)
        sPendingActivation = nullptr;

    delete document;
}

static void ActivateMostRecentDocument()
{
    if (Document* next = FindMostRecentDocument(true))
    {
        ActivateDocument(next);
        return;
    }

    sSoundPlayer.setOwnerContext(nullptr);
    RefreshWindowTitle();

    RequestDocumentActivation(FindMostRecentDocument(false));
}

static void InsertDocument(Document* document, Document* replaced)
{
    auto position = sDocuments.end();

    if (replaced)
    {
        const s64 index = std::find(sDocuments.begin(), sDocuments.end(), replaced) - sDocuments.begin();

        RemoveDocument(replaced);
        position = sDocuments.begin() + std::min<s64>(index, static_cast<s64>(sDocuments.size()));
    }

    document->state().memoryEstimate = document->estimateMemory();

    RefreshDocumentPath(document);

    sDocuments.insert(position, document);

    ActivateDocument(document);
}

void AddDocument(Document* document, bool isPreview)
{
    Document* preview = isPreview ? FindPreviewDocument() : nullptr;

    document->state().isPreview = isPreview;

    InsertDocument(document, preview && !preview->state().unsaved ? preview : nullptr);
}

void ReplaceDocument(Document* replaced, Document* document)
{
    document->state().isPreview = replaced->state().isPreview;

    InsertDocument(document, replaced);
}

bool ActivateDocument(Document* document)
{
    if (!document)
        return false;

    if (!document->isLoaded())
    {
        document->state().reloadFailed = !ReloadDocument(*document);

        if (document->state().reloadFailed)
            return false;
    }

    if (sActive != document)
    {
        sActive = document;

        if (!sHasFocus)
            SwitchToParseContextOf(document);
    }

    document->state().lastActive = ++sLastActivation;

    sSoundPlayer.setOwnerContext(document);

    RefreshWindowTitle();
    RecordDocumentSession();
    EnforceDocumentMemoryBudget();

    return true;
}

void RequestDocumentActivation(Document* document)
{
    if (document && document->isLoaded())
        ActivateDocument(document);
    else
        sPendingActivation = document;
}

void UpdatePendingActivation()
{
    Document* document = sPendingActivation;
    sPendingActivation = nullptr;

    if (document && !ActivateDocument(document) && !sActive)
        ActivateMostRecentDocument();
}

void CloseDocument(Document* document)
{
    const bool wasActive = document == sActive;

    RemoveDocument(document);

    if (wasActive)
        ActivateMostRecentDocument();

    RecordDocumentSession();
}

void CloseAllDocuments()
{
    while (!sDocuments.empty())
        RemoveDocument(sDocuments.back());

    sSoundPlayer.setOwnerContext(nullptr);
    RefreshWindowTitle();
}

void PinDocument(Document* document)
{
    if (!document || !document->state().isPreview)
        return;

    document->state().isPreview = false;

    if (!document->filePath().isEmpty())
        AddRecentFile(document->filePath().cstr());

    RecordDocumentSession();
}

bool IsDocumentSwitchLocked()
{
    return ImGui::GetTopMostPopupModal() != nullptr || IsSoundExportRunning();
}

void RefreshDocumentPath(Document* document)
{
    const sead::SafeString& path = document->filePath().isEmpty() ? document->loadedPath() : document->filePath();

    document->state().canonicalPath = path.isEmpty() ? std::string() : CanonicalDocumentPath(path.cstr());
}

void RefreshWindowTitle()
{
    const char* appName = IsPlayerFrameShown() ? messages::playerframe::cWindowTitle : nullptr;

    if (!sActive)
    {
        util::updateTitle(nullptr, false, appName);
        return;
    }

    util::updateTitle(sActive->displayName().c_str(), sActive->state().unsaved, appName);
}

void RecordDocumentSession()
{
    std::vector<std::string> paths;

    for (const Document* document : sDocuments)
    {
        if (!document->state().isPreview && !document->filePath().isEmpty())
            paths.push_back(document->filePath().cstr());
    }

    const bool hasActivePath = sActive && !sActive->filePath().isEmpty();

    SetSessionDocuments(paths, hasActivePath ? sActive->filePath().cstr() : "");
}

static Document* FindUnloadCandidate()
{
    Document* oldest = nullptr;

    for (Document* document : sDocuments)
    {
        const DocumentState& state = document->state();

        if (document == sActive || !document->canUnload() || state.unsaved || sSoundPlayer.isPlayingFrom(document) || DocumentHasChanges(*document))
            continue;

        if (!oldest || state.lastActive < oldest->state().lastActive)
            oldest = document;
    }

    return oldest;
}

void EnforceDocumentMemoryBudget()
{
    u64 total = 0;

    for (const Document* document : sDocuments)
        total += document->state().memoryEstimate;

    const u64 budget = GetMemoryBudget();

    while (total > budget)
    {
        Document* candidate = FindUnloadCandidate();

        if (!candidate)
            return;

        total -= candidate->state().memoryEstimate;

        UnloadDocument(*candidate);
    }
}
