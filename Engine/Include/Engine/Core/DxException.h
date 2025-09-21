// GfxException.h
#pragma once
#include <string>
#include <exception>
#include <windows.h>
#include <comdef.h>
#include <filesystem>
#include <iostream>

/**
 * @brief Exception class for Direct3D/WinAPI failures.
 *
 * Stores an HRESULT error code together with contextual information
 * about where the failure occurred (function name, source file, line number).
 * Used in combination with the ThrowIfFailed macro to provide detailed
 * error reporting.
 */
class DxException : public std::exception
{
public:
    /**
     * @brief Construct an exception object.
     * @param hr - HRESULT error code that was returned by a failed API call
     * @param functionName - name of the function (expression) that failed
     * @param filename - source file where the error occurred
     * @param lineNumber - line number in the source file
     */
    DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& fileName, int lineNumber, std::wstring details = {})
        : hr_(hr), functionName_(std::move(functionName)), fileName_(std::move(fileName)), lineNumber_(lineNumber), details_(std::move(details))
    {
        _com_error err(hr_);
        std::wstring wmsg = err.ErrorMessage() ? err.ErrorMessage() : L"";
        std::wstring wall =
            L"Function: " + functionName_ + L"\n" +
            L"File: " + fileName_ + L":" + std::to_wstring(lineNumber_) + L"\n" +
            L"HRESULT: 0x" + ToHex(hr_) + L" — " + wmsg +
            (details_.empty() ? L"" : L"\n\nDetails:\n" + details_);

        narrow_ = WideToUtf8(wall);
    }

    /// @brief Standard what() message in UTF-8
    const char* what() const noexcept override { return narrow_.c_str(); }

    /// @brief Full wide message (for MessageBoxW)
    std::wstring WideMessage(const std::wstring& title = L"Undefined Error") const {
        _com_error err(hr_);
        const wchar_t* sys = err.ErrorMessage();
        std::wstring sysMsg = sys ? sys : L"";

        std::wstringstream ss;
        ss << L"[" << title << L"]\n"
            << L"-----------------------------------\n"
            << L"Operation: " << functionName_ << L"\n"
            << L"Location:  " << fileName_ << L":" << lineNumber_ << L"\n"
            << L"HRESULT:   0x" << ToHex(hr_) << L"\n"
            << L"Message:   " << sysMsg << L"\n";

        if (!details_.empty()) {
            ss << L"\nDetails: " << details_ << L"\n";
        }
        return ss.str();
    }

    /**
     * @brief Get a formatted string description of the error.
     * @return String containing function name, file, line, and system error message.
     */
    std::wstring ToString() const
    {
        // Get the string description of the error code.
        _com_error err(hr_);
        std::wstring msg = err.ErrorMessage();
        
        return functionName_ + L" failed in " + fileName_ + L"; line " + std::to_wstring(lineNumber_) + L"; error: " + msg;
    }

    HRESULT code() const noexcept { return hr_; }

private:
    static std::wstring ToHex(HRESULT hr) {
        wchar_t buf[16]{};
        swprintf(buf, 16, L"%08X", static_cast<unsigned>(hr));
        return buf;
    }

    // Simple UTF-16 -> UTF-8 for what()
    static std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(needed, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], needed, nullptr, nullptr);
        return s;
    }

    HRESULT hr_ = S_OK;
    std::wstring functionName_;
    std::wstring fileName_;
    int lineNumber_ = -1;
    std::wstring details_;
    std::string narrow_;
};


class DxWarning
{
public:
    DxWarning(const std::wstring& functionName,
        const std::wstring& fileName,
        int lineNumber,
        const std::wstring& details = L"")
        : functionName_(functionName),
        fileName_(fileName),
        lineNumber_(lineNumber),
        details_(details)
    {
        std::wstringstream ss;
        ss << "\n" << fileName_ << L"(" << lineNumber_ << L") " << L"warning: ";


        if (!details_.empty()) ss << details_;
        ss << L"\n";

        message_ = ss.str();
    }

    const std::wstring& Message() const { return message_; }

    const void sendMessage() { OutputDebugStringW(message_.c_str()); }

private:
    std::wstring functionName_;
    std::wstring fileName_;
    int lineNumber_;
    std::wstring details_;
    std::wstring message_;
};


// ANSI file to wide
inline std::wstring AnsiToWideFile(const char* f) {
    int n = MultiByteToWideChar(CP_ACP, 0, f, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_ACP, 0, f, -1, &w[0], n);
    return w;
}

/**
 * @brief Replace all spaces in a wide string with non-breaking spaces (NBSP).
 *
 * Iterates through the input string and replaces each U+0020 (regular space)
 * with U+00A0 (non-breaking space). A non-breaking space looks like a normal
 * space but prevents automatic line breaking at that position in many UI
 * elements (e.g., MessageBox, rich text).
 *
 * Useful when displaying file paths or identifiers in UI elements
 * that may otherwise insert line breaks at spaces.
 *
 * @param str - input wide string (std::wstring). The original string is not modified.
 *
 * @return A copy of the input string where all spaces have been replaced by NBSP.
 */
static std::wstring MakeNoWrap(const std::wstring& str) {
    std::wstring strCopy = str;

    // Replace regular spaces with non-breaking spaces (NBSP)
    // so that the system does not break the line at this space by default
    for (auto& ch : strCopy) if (ch == L' ') ch = L'\u00A0';

    return strCopy;
}

//inline void ThrowError(const std::wstring& operation, const std::wstring& details = L"")
//{
//    throw DxException(E_FAIL, operation, AnsiToWideFile(__FILE__), __LINE__, details);
//}

inline void LogWarningAt(const char* file, int line, const std::wstring& operation, const std::wstring& details = L"")
{
    auto warning = DxWarning(operation, MakeNoWrap(AnsiToWideFile(file)), line, details);
    warning.sendMessage();
}

#define ThrowWarning(operation, details) \
    do { LogWarningAt(__FILE__, __LINE__, (operation), (details)); } while(0)


inline void ThrowErrorAt(const char* file, int line, const std::wstring& operation, const std::wstring& details = L"")
{
    throw DxException(E_FAIL, operation, MakeNoWrap(AnsiToWideFile(file)), line, details);
}

#define ThrowError(operation, details) \
    do { ThrowErrorAt(__FILE__, __LINE__, (operation), (details)); } while(0)


/**
 * @brief Throw if FAILED(hr). Details = empty.
 */
#define ThrowIfFailed(hrVar)                                                                                     \
    do {                                                                                                         \
        HRESULT _hr__ = (hrVar);                                                                                 \
        if (FAILED(_hr__)) {                                                                                     \
            throw DxException(_hr__, std::wstring(L#hrVar), AnsiToWideFile(__FILE__), __LINE__, std::wstring()); \
        }                                                                                                        \
    } while (0)

 /**
  * @brief Throw if FAILED(hr). Caller provides a details wide-string.
  */
#define ThrowIfFailedMsg(hrVar, operationNameWide, detailsWide)                                                \
    do {                                                                                                       \
        HRESULT _hr__ = (hrVar);                                                                               \
        if (FAILED(_hr__)) {                                                                                   \
            throw DxException(_hr__, (operationNameWide), MakeNoWrap(AnsiToWideFile(__FILE__)), __LINE__, (detailsWide));  \
        }                                                                                                      \
    } while (0)