#pragma once

#include <basis/seadTypes.h>
#include <prim/seadEndian.h>

#include <string>
#include <vector>

class Document;

const std::vector<Document*>& GetOpenDocuments();
Document* FindOpenDocument(const char* path);
Document* FindPreviewDocument();
bool HasUnsavedDocuments();
std::string CanonicalDocumentPath(const char* path);

void AddDocument(Document* document, bool isPreview);
void ReplaceDocument(Document* replaced, Document* document);
bool ActivateDocument(Document* document);
void RequestDocumentActivation(Document* document);
void UpdatePendingActivation();
void CloseDocument(Document* document);
void CloseAllDocuments();
void PinDocument(Document* document);

bool IsDocumentSwitchLocked();
void PrepareParseEndian(sead::Endian::Types endian);

void RefreshDocumentPath(Document* document);
void RefreshWindowTitle();
void RecordDocumentSession();
void EnforceDocumentMemoryBudget();

class DocumentFocus
{
public:
    explicit DocumentFocus(Document* document);
    ~DocumentFocus();

    DocumentFocus(const DocumentFocus&) = delete;
    DocumentFocus& operator=(const DocumentFocus&) = delete;

private:
    bool mHadFocus;
    Document* mPrevious;
    bool mLockedSound;
};
