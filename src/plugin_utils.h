#pragma once

#include <algorithm>
#include <cctype>
#include <string>

inline void parseBool(const std::string& val, bool& out) {
    if (val == "1" || val == "true" || val == "TRUE" || val == "yes" || val == "on") {
        out = true;
    } else if (val == "0" || val == "false" || val == "FALSE" || val == "no" || val == "off") {
        out = false;
    }
}

inline std::string bool01(bool value) {
    return value ? "1" : "0";
}

inline std::string trimString(const std::string& s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    auto start = std::find_if(s.begin(), s.end(), notSpace);
    auto end = std::find_if(s.rbegin(), s.rend(), notSpace).base();
    if (start >= end) {
        return {};
    }
    return std::string(start, end);
}

inline std::string trimNull(const std::string& s) {
    auto nul = s.find('\0');
    if (nul == std::string::npos) {
        return s;
    }
    return s.substr(0, nul);
}
