#pragma once

#include <bfsar/model/Item.h>

#include <imgui/imgui.h>

class FileWindow : public Item
{
public:
    FileWindow(Item* file)
        : mOpen(true)
        , mFileRef(this)
    {
        mFileRef.attach(file);

        file->onOpenFileWindow();
    }

    bool isFileWindow() const override
    {
        return true;
    }

    ~FileWindow() override
    {
        if (mFileRef.isAttached())
        {
            mFileRef.getItem()->onCloseFileWindow();
        }
    }

    bool isOpen() const
    {
        return mOpen;
    }

    bool* getOpenPtr()
    {
        return &mOpen;
    }

    const ItemReference& getFileRef() const
    {
        return mFileRef;
    }

    ItemReference& getFileRef()
    {
        return mFileRef;
    }

private:
    bool mOpen;
    ItemReference mFileRef;
};

FileWindow* OpenFileWindow(Item* item);
void CloseAllFileWindows();
void DrawFileWindows(ImGuiID dockspaceId);
