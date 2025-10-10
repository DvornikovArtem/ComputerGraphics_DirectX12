// Engine/UI/DebugOutputHook.cpp

// Hooks OutputDebugString and mirrors it into an in-game console using MinHook

#include <windows.h>
#include <string>
#include <mutex>

#include <MinHook.h>
#include <Engine/UI/ImGui_Layer.h>



using Engine::UI::DebugConsole;


// Typedefs for pointers to the original OutputDebugStringA/W functions
// ANSI variant, takes LPCSTR
using PFN_OutputDebugStringA = VOID(WINAPI*)(LPCSTR);
// Wide variant, takes LPCWSTR.
using PFN_OutputDebugStringW = VOID(WINAPI*)(LPCWSTR);


// Global pointers to the original functions so we can call them after our hook
// Initialized to null meaning "not set yet"
static PFN_OutputDebugStringA s_OrigA = nullptr;
// Same for the wide function
static PFN_OutputDebugStringW s_OrigW = nullptr;



/**
 * Intercepts ANSI strings from OutputDebugStringA and forwards them to the in-game console.
 *
 * @param s: Pointer to an ANSI C-string (may be nullptr).
 *
 * @return
 *   Returns nothing. Matches the original WinAPI function signature (VOID).
 *
 * @remarks
 *   Uses g_ConsoleEchoGuard to avoid echo loops when the console writes output itself.
 */
static VOID WINAPI Hook_OutputDebugStringA(LPCSTR s)
{
    // If echo guard is off, it's safe to append to console
    if (!Engine::UI::g_ConsoleEchoGuard) {
        Engine::UI::DebugConsole::Get().AppendFromHook(s ? s : "");
    }

    // Call the original function if available
    if (s_OrigA) s_OrigA(s);
}



/**
 * Intercepts wide strings from OutputDebugStringW, converts to UTF-8, and forwards them to the in-game console.
 *
 * @param ws: Pointer to a wide C-string (may be nullptr).
 *
 * @return
 *   Returns nothing. Matches the original WinAPI function signature (VOID).
 *
 * @remarks
 *   Uses g_ConsoleEchoGuard to avoid echo loops when the console writes output itself.
 */
static VOID WINAPI Hook_OutputDebugStringW(LPCWSTR ws)
{
    if (!Engine::UI::g_ConsoleEchoGuard) {
        std::string u8;
        if (ws) {
            int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);

            // >1 means at least one char besides the terminator
            if (len > 1) {
                // Resize to len-1 (exclude the trailing NUL)
                u8.resize(size_t(len - 1));
                // Convert wide string to UTF-8
                WideCharToMultiByte(CP_UTF8, 0, ws, -1, u8.data(), len, nullptr, nullptr);
            }
        }
        Engine::UI::DebugConsole::Get().AppendFromHook(u8.c_str());
    }
    if (s_OrigW) s_OrigW(ws);
}



/**
 * Initializes MinHook, locates OutputDebugStringA/W in kernel32.dll, creates detours and enables them.
 *
 * @return bool
 *   true on success; false on any error (init, address lookup, hook creation, or enabling hooks).
 */
bool InstallDebugOutputHooks()
{
    // Initialize MinHook. Bail out on failure.
    if (MH_Initialize() != MH_OK) return false;

    // Get handle to kernel32.dll
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) return false;

    // Locate address of ANSI function
    auto pA = (LPVOID)GetProcAddress(hK32, "OutputDebugStringA");
    // Locate address of wide function
    auto pW = (LPVOID)GetProcAddress(hK32, "OutputDebugStringW");
    if (!pA || !pW) return false;

    // Create hook for A version; store original in s_OrigA; fail on error
    if (MH_CreateHook(pA, &Hook_OutputDebugStringA, reinterpret_cast<LPVOID*>(&s_OrigA)) != MH_OK) return false;
    // Create hook for W version; store original in s_OrigW; fail on error
    if (MH_CreateHook(pW, &Hook_OutputDebugStringW, reinterpret_cast<LPVOID*>(&s_OrigW)) != MH_OK) return false;

    // Enable the A-hook; fail on error
    if (MH_EnableHook(pA) != MH_OK) return false;
    // Enable the W-hook; fail on error
    if (MH_EnableHook(pW) != MH_OK) return false;

    return true;
}



/**
 * Disables all active hooks and releases MinHook resources.
 *
 * @return
 *   Returns nothing.
 */
void UninstallDebugOutputHooks()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}
