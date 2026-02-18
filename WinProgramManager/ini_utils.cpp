#include "ini_utils.h"
#include <windows.h>
#include <fstream>
#include <vector>

static std::wstring ToW(const std::string& s, UINT codepage) {
    if (s.empty()) return L"";
    int needed = MultiByteToWideChar(codepage, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (needed == 0) return L"";
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(codepage, 0, s.c_str(), (int)s.size(), &out[0], needed);
    return out;
}

static std::string NarrowPath(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string s(n-1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    return s;
}

std::wstring ReadIniValue(const std::wstring& iniPath, const std::wstring& section, const std::wstring& key) {
    // Read raw bytes
    std::ifstream f(NarrowPath(iniPath), std::ios::binary);
    if (!f) return L"";
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) return L"";

    // Detect BOM
    std::wstring text;
    if (buf.size() >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) {
        // UTF-16 LE
        int wcnt = (int)((buf.size() - 2) / 2);
        text.assign((wchar_t*)(buf.data() + 2), (size_t)wcnt);
    } else if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        // UTF-8 with BOM
        std::string s(buf.begin() + 3, buf.end());
        text = ToW(s, CP_UTF8);
    } else {
        // Assume ANSI / OEM
        std::string s(buf.begin(), buf.end());
        // Try UTF-8 first
        text = ToW(s, CP_UTF8);
        // If contains many replacement chars (zero-length result), fallback to ANSI
        if (text.empty()) text = ToW(s, CP_ACP);
    }

    // Normalize line endings
    for (auto &c : text) if (c == L'\r') c = L'\n';

    // Find section
    std::wstring targetSec = L"[" + section + L"]";
    size_t pos = text.find(targetSec);
    size_t start = 0;
    if (pos != std::wstring::npos) start = pos + targetSec.size();
    // Search lines from start
    size_t cur = start;
    while (cur < text.size()) {
        size_t lineEnd = text.find(L'\n', cur);
        if (lineEnd == std::wstring::npos) lineEnd = text.size();
        std::wstring line = text.substr(cur, lineEnd - cur);
        // Trim
        size_t a = 0; while (a < line.size() && iswspace(line[a])) ++a;
        size_t b = line.size(); while (b > a && iswspace(line[b-1])) --b;
        if (b > a) {
            std::wstring t = line.substr(a, b-a);
            if (!t.empty() && t[0] == L'[') break; // next section
            size_t eq = t.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring k = t.substr(0, eq);
                std::wstring v = t.substr(eq+1);
                // trim k
                size_t aa = 0; while (aa < k.size() && iswspace(k[aa])) ++aa;
                size_t bb = k.size(); while (bb > aa && iswspace(k[bb-1])) --bb;
                k = k.substr(aa, bb-aa);
                if (_wcsicmp(k.c_str(), key.c_str()) == 0) return v;
            }
        }
        cur = lineEnd + 1;
    }
    return L"";
}

bool WriteSettingsIniUtf8(const std::wstring& iniPath, const std::wstring& language, const std::wstring& updaterTaskCreated) {
    // Ensure directory exists
    size_t p = iniPath.find_last_of(L"\\/");
    if (p != std::wstring::npos) {
        std::wstring dir = iniPath.substr(0, p);
        CreateDirectoryW(dir.c_str(), NULL);
    }
    // Build UTF-8 content
    std::string content;
    content += "[Settings]\r\n";
    content += "Language=";
    // convert language to UTF-8
    if (!language.empty()) {
        int n = WideCharToMultiByte(CP_UTF8, 0, language.c_str(), (int)language.size(), NULL, 0, NULL, NULL);
        if (n > 0) {
            std::string tmp(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, language.c_str(), (int)language.size(), &tmp[0], n, NULL, NULL);
            content += tmp;
        }
    }
    content += "\r\n";
    content += "UpdaterTaskCreated=";
    if (!updaterTaskCreated.empty()) {
        int n = WideCharToMultiByte(CP_UTF8, 0, updaterTaskCreated.c_str(), (int)updaterTaskCreated.size(), NULL, 0, NULL, NULL);
        if (n > 0) {
            std::string tmp(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, updaterTaskCreated.c_str(), (int)updaterTaskCreated.size(), &tmp[0], n, NULL, NULL);
            content += tmp;
        }
    }
    content += "\r\n";

    // Write with UTF-8 BOM
    std::ofstream out(NarrowPath(iniPath), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    unsigned char bom[3] = {0xEF,0xBB,0xBF};
    out.write((char*)bom, 3);
    out.write(content.c_str(), (std::streamsize)content.size());
    out.close();
    return true;
}
