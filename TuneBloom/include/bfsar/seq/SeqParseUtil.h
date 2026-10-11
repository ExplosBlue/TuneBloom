#pragma once

#include <string>
#include <vector>

class MmlCommandBase;

// trim from left
inline std::string& ltrim(std::string& s, const char* t = " \t\n\r\f\v")
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from right
inline std::string& rtrim(std::string& s, const char* t = " \t\n\r\f\v")
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from left & right
inline std::string& trim(std::string& s, const char* t = " \t\n\r\f\v")
{
    return ltrim(rtrim(s, t), t);
}

std::vector<std::string> SplitString(const std::string& text, const std::string& delimiter);

bool ParseSeqInt(int* out, std::string str);
bool IsSeqLabelChar(int c);

const char* GetSeqVariableName(unsigned int varNo);

MmlCommandBase* ParseSeqCommand(const std::string& str, const std::vector<std::string>& args, std::string& errorMsg);
