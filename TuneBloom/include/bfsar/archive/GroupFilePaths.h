#pragma once

#include <prim/seadSafeString.h>

#include <string>

namespace groupfilepaths
{

void Select(const sead::SafeString& archivePath, const sead::SafeString& groupName, const std::string& path, bool keep);
void Remember(const sead::SafeString& archivePath, const sead::SafeString& groupName, const std::string& path, bool persist);
std::string Find(const sead::SafeString& archivePath, const sead::SafeString& groupName, bool persisted);

}
