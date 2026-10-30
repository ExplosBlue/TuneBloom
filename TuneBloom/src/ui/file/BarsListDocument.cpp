#include <ui/file/BarsListDocument.h>

#include <ui/file/LooseFileInfo.h>
#include <messages/Messages.h>
#include <ui/resource/BarsListViews.h>

#include <scan/FileProbe.h>

#include <filesystem>

Document* BarsListDocument::Open(File* file, const sead::SafeString& path)
{
    return OpenAs(new BarsListDocument(), file, path);
}

BarsListDocument::~BarsListDocument()
{
    closeWindows();
}

BarsListFile& BarsListDocument::file()
{
    return mFile;
}

Document::Kind BarsListDocument::kind() const
{
    return Kind::BarsList;
}

const sead::SafeString& BarsListDocument::filePath() const
{
    return mFile.getFilePath();
}

std::string BarsListDocument::displayName() const
{
    return std::filesystem::path(mFile.getFilePath().cstr()).filename().string();
}

BrowserFileKind BarsListDocument::fileKind() const
{
    return BrowserFileKind::ResourceList;
}

UIType BarsListDocument::defaultTab() const
{
    return UIType::AudioAssets;
}

void BarsListDocument::drawInfo(UIType)
{
    DrawBarsListUI(mFile);
}

bool BarsListDocument::drawFileProperties()
{
    DrawBarsListInfoUI(mFile);
    return true;
}

StreamSource BarsListDocument::activeStreamSource()
{
    return {};
}

bool BarsListDocument::isValidName(const sead::SafeString& name) const
{
    return BarsListFile::IsValidEntryName(name);
}

const char* BarsListDocument::getNameRuleDetail() const
{
    return messages::barslist::cNameInvalidDetail;
}

bool BarsListDocument::isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const
{
    return mFile.isUniqueEntryName(name, ignoreItem);
}

void BarsListDocument::renumber(Item::List&)
{
    mFile.renumber();
}

void BarsListDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mFile.getEntryList());
}

bool BarsListDocument::isInfoModified() const
{
    return mFile.isModified();
}

bool BarsListDocument::ownsItem(const Item& item) const
{
    return item.list() == &mFile.getEntryList();
}

SaveResult BarsListDocument::save(const char** outError)
{
    return mFile.save(outError);
}

SaveResult BarsListDocument::saveAs(const sead::SafeString& path, const char** outError)
{
    return mFile.saveAs(path, outError);
}

LooseFileFormat BarsListDocument::fileFormat() const
{
    return GetBarsListFormat(mFile);
}

bool BarsListDocument::openFile(const File& file, const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    if (mFile.open(file.data, file.size, path))
        return true;

    outError->copy(messages::barslist::cNotBarsList);
    return false;
}

bool BarsListDocument::writeTo(const sead::SafeString& path) const
{
    return mFile.writeTo(path);
}

BarsListFile* FindActiveBarsList()
{
    Document* document = ActiveDocument();

    if (!document || document->kind() != Document::Kind::BarsList)
        return nullptr;

    return &static_cast<BarsListDocument*>(document)->file();
}
