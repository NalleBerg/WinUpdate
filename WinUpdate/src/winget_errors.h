#pragma once
#include <string>

// Winget exit codes and error handling utilities
// Based on official Microsoft documentation:
// https://github.com/microsoft/winget-cli/blob/master/doc/windows/package-manager/winget/returnCodes.md

namespace WingetErrors {

// Common winget exit codes (as signed int to match DWORD when cast)
constexpr DWORD SUCCESS = 0;
constexpr DWORD DOWNLOAD_FAILED = 0x8A150008;                // -1978335224 (decimal)
constexpr DWORD NO_APPLICABLE_INSTALLER = 0x8A150010;        // -1978335216
constexpr DWORD NO_APPLICATIONS_FOUND = 0x8A150014;          // -1978335212
constexpr DWORD UPDATE_NOT_APPLICABLE = 0x8A15002B;          // -1978335189
constexpr DWORD PACKAGE_ALREADY_INSTALLED = 0x8A150061;      // -1978335135
constexpr DWORD INSTALL_CANCELLED_BY_USER = 0x8A15010C;      // -1978334964
constexpr DWORD WINDOWS_ERROR_CANCELLED = 0x800704C7;        // 2147943623 (positive)
constexpr DWORD TIMEOUT = 0xFFFFFFFE;                        // -2 (our internal timeout marker)

// Error severity levels
enum class ErrorLevel {
    SUCCESS,          // ✓ Green - Successful
    INFO,             // ℹ️ Dark blue - Informational (skipped, already updated)
    WARNING,          // ⚠️ Orange - Warning (user cancelled, suggest exclude)
    FAILURE           // ❌ Red - Error (failed)
};

// Get error level from exit code
inline ErrorLevel GetErrorLevel(DWORD exitCode) {
    if (exitCode == SUCCESS) {
        return ErrorLevel::SUCCESS;
    }
    if (exitCode == UPDATE_NOT_APPLICABLE || 
        exitCode == NO_APPLICABLE_INSTALLER || 
        exitCode == PACKAGE_ALREADY_INSTALLED) {
        return ErrorLevel::INFO;  // Skip - not an error, just not applicable
    }
    if (exitCode == INSTALL_CANCELLED_BY_USER || 
        exitCode == WINDOWS_ERROR_CANCELLED || 
        exitCode == NO_APPLICATIONS_FOUND) {
        return ErrorLevel::WARNING;  // Recommend exclude
    }
    return ErrorLevel::FAILURE;  // Everything else is a failure
}

// Get user-friendly recommendation message (ADDS to winget's output, doesn't replace)
inline std::wstring GetErrorMessage(DWORD exitCode) {
    // Convert to signed for display
    int signedCode = (int)exitCode;
    
    // Build exit code line
    wchar_t codeHex[32];
    swprintf(codeHex, 32, L"0x%08X", exitCode);
    std::wstring exitCodeLine = L"Exit code: " + std::wstring(codeHex) + L" (" + std::to_wstring(signedCode) + L")\r\n";
    
    // Add recommendation based on error level
    switch (exitCode) {
        case SUCCESS:
            return exitCodeLine;  // No recommendation needed for success
        
        case UPDATE_NOT_APPLICABLE:
            return exitCodeLine +
                   L"ℹ️ Recommendation: Skip this package and wait for the next version.\r\n"
                   L"   A newer version is available but it doesn't match your system requirements\r\n"
                   L"   (e.g., different Windows version, architecture, or dependencies).\r\n"
                   L"   WinUpdate will automatically check for applicable updates in future scans.";
        
        case NO_APPLICABLE_INSTALLER:
            return exitCodeLine +
                   L"ℹ️ Recommendation: Skip this package - no compatible installer available.\r\n"
                   L"   The publisher hasn't released an installer for your system configuration.\r\n"
                   L"   This typically means the package requires a different Windows version,\r\n"
                   L"   processor architecture (x64/ARM), or has unmet system requirements.";
        
        case PACKAGE_ALREADY_INSTALLED:
            return exitCodeLine +
                   L"ℹ️ This package is already up to date - no action needed.";
        
        case INSTALL_CANCELLED_BY_USER:
        case WINDOWS_ERROR_CANCELLED:
            return exitCodeLine +
                   L"⚠️ Recommendation: Exclude this package if you want to stop update prompts.\r\n"
                   L"   The installation was cancelled (either by you or by the installer itself).\r\n"
                   L"   If you don't want to install this package, exclude it from future updates\r\n"
                   L"   using the system tray menu: Right-click → Manage Exclusions.";
        
        case NO_APPLICATIONS_FOUND:
            return exitCodeLine +
                   L"⚠️ Recommendation: Exclude this package - it's no longer available.\r\n"
                   L"   The package cannot be found in any configured package source.\r\n"
                   L"   This usually means the publisher has removed, discontinued, or renamed it.\r\n"
                   L"   Excluding it will prevent repeated failed update attempts.";
        
        case DOWNLOAD_FAILED:
            return exitCodeLine +
                   L"❌ Recommendation: Retry the installation when network conditions improve.\r\n"
                   L"   The package download failed due to network connectivity or server issues.\r\n"
                   L"   Verify your internet connection is stable and try the update again later.\r\n"
                   L"   If the problem persists, the publisher's download server may be down.";
        
        case TIMEOUT:
            return exitCodeLine +
                   L"❌ Installation timed out - the operation took longer than expected.\r\n"
                   L"   The installer may be waiting for your input in a hidden window,\r\n"
                   L"   or it encountered an issue that caused it to hang.\r\n"
                   L"   Check Task Manager for stuck installer processes and try again.";
        
        default:
            // For unknown error codes, show generic message
            return exitCodeLine +
                   L"❌ Installation failed with an unknown error.\r\n"
                   L"   Search online for this exit code (hex and decimal values above)\r\n"
                   L"   to find specific information about what went wrong.\r\n"
                   L"   Check the package publisher's website or support forums for help.";
    }
}

// Get status icon based on error level
inline std::wstring GetStatusIcon(ErrorLevel level) {
    switch (level) {
        case ErrorLevel::SUCCESS:
            return L"✓";
        case ErrorLevel::INFO:
            return L"ℹ️";
        case ErrorLevel::WARNING:
            return L"⚠️";
        case ErrorLevel::FAILURE:
            return L"❌";
        default:
            return L"?";
    }
}

// Get short status text (includes exit code for context)
inline std::wstring GetStatusText(DWORD exitCode) {
    ErrorLevel level = GetErrorLevel(exitCode);
    std::wstring icon = GetStatusIcon(level);
    
    // Convert to signed for display
    int signedCode = (int)exitCode;
    wchar_t codeHex[32];
    swprintf(codeHex, 32, L"0x%08X", exitCode);
    
    switch (exitCode) {
        case SUCCESS:
            return icon + L" Success";
        case UPDATE_NOT_APPLICABLE:
            return icon + L" Skipped - No applicable upgrade";
        case NO_APPLICABLE_INSTALLER:
            return icon + L" Skipped - No applicable installer";
        case PACKAGE_ALREADY_INSTALLED:
            return icon + L" Skipped - Already up to date";
        case INSTALL_CANCELLED_BY_USER:
        case WINDOWS_ERROR_CANCELLED:
            return icon + L" Cancelled by user";
        case NO_APPLICATIONS_FOUND:
            return icon + L" Failed - Package not found";
        case DOWNLOAD_FAILED:
            return icon + L" Failed - Download failed";
        case TIMEOUT:
            return icon + L" Failed - Timeout";
        default:
            return icon + L" Failed (exit code: " + std::wstring(codeHex) + L")";
    }
}

// Check if this exit code should be counted as a failure (vs skip/info)
inline bool IsFailure(DWORD exitCode) {
    return GetErrorLevel(exitCode) == ErrorLevel::FAILURE;
}

// Check if this exit code should be counted as a skip (not failure, not success)
inline bool IsSkipped(DWORD exitCode) {
    return GetErrorLevel(exitCode) == ErrorLevel::INFO;
}

} // namespace WingetErrors
