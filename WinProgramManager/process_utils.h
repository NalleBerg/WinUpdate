// Small process helpers for finding, gracefully closing and force-killing processes
#pragma once
#include <windows.h>
#include <string>
#include <vector>

std::vector<DWORD> FindProcessesByExe(const std::wstring& exeNameOrPath);
std::vector<HWND> EnumProcessTopWindows(DWORD pid);
bool SendGracefulClose(DWORD pid, DWORD timeoutMs);
bool ForceKillProcess(DWORD pid);
bool IsProcessRunning(DWORD pid);
