// ImGui_Layer.h

#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#pragma once

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <string>
#include <unordered_map>

#include <Engine/UI/UIPanel.h>
#include <filesystem>
#include <deque>
#include <mutex>



namespace Engine::UI
{
    extern thread_local bool g_ConsoleEchoGuard;

    class ImGuiLayer {
    public:
        struct Desc {
            HWND hwnd = nullptr;
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* cmdQueue = nullptr;
            DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            // The ImGui pipeline renders 2D meshes on top of the frame with depth testing disabled, so depth is not required
            DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
            int framesInFlight = 3;
            UINT srvHeapCapacity = 64;
        };

        ~ImGuiLayer();

        void Initialize(const Desc& d);
        void Shutdown();

        // ImGui_ImplDX12_NewFrame + ImGui_ImplWin32_NewFrame + ImGui::NewFrame()
        void NewFrame();
        void RenderDrawData(ID3D12GraphicsCommandList* cmdList);

        ID3D12DescriptorHeap* GetSrvHeap() const { return mSrvHeap.Get(); }

        void AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE& outCPU, D3D12_GPU_DESCRIPTOR_HANDLE& outGPU);

        D3D12_GPU_DESCRIPTOR_HANDLE CreateTextureSRV(ID3D12Resource* tex, DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM);

        D3D12_GPU_DESCRIPTOR_HANDLE CreateOrOverwriteTextureSRV(ID3D12Resource* tex, DXGI_FORMAT fmt, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {});

        void CopySrvIntoSlot(D3D12_GPU_DESCRIPTOR_HANDLE dstGpuHandle, D3D12_CPU_DESCRIPTOR_HANDLE srcCpuHandle);

        ImTextureID GetOrCreateImTextureIDFromSrvCPU(D3D12_CPU_DESCRIPTOR_HANDLE srcSrvCPU);

        struct TextureView {
            ID3D12Resource* resource;
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu;
            std::string label;
        };

        void OnDpiChanged(float newDpi) const;


        void DrawTextureGridFixedSize_ImTexID(const char* windowTitle, const std::vector<std::pair<ImTextureID, std::string>>& textures, int rows, int cols, ImVec2 tileSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1), float cellPadding = 0.0f, bool showLabels = true);

        // Load layout right now
        bool load_layout_now(const std::filesystem::path& file);

        // Save layout right now
        bool save_layout_now(const std::filesystem::path& file) const;

        // Load the layout at the beginning of the next frame (after all ImGui windows have been properly processed in the previous frame)
        void queue_load_layout(const std::filesystem::path& file);

        // Save the layout at the end of the current frame (so that all ImGui windows are properly processed and saved)
        void queue_save_layout(const std::filesystem::path& file);

        void DrawBuiltins();

        void ShowConsole(bool v) { mShowConsole = v; }
        bool IsConsoleVisible() const { return mShowConsole; }

    private:
        ImGuiLayer::Desc cfg{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
        UINT mDescriptorSize = 0;
        UINT mSrvAllocIndex = 0;

        // To shut down correctly even if Initialize() was not called
        bool mInitialized = false;

        std::unordered_map<SIZE_T, D3D12_GPU_DESCRIPTOR_HANDLE> mSrvCpuToGpu;


        enum class LayoutOpKind { None, Load, Save };
        struct LayoutOp
        {
            LayoutOpKind kind = LayoutOpKind::None;
            std::filesystem::path path;
        };
        LayoutOp m_pendingLayoutOpBegin;
        LayoutOp m_pendingLayoutOpEnd;

        bool mShowConsole = true;
    };

    class DebugConsole {
    public:
        enum class Level { Info, Warn, Error };

        // Singleton
        static DebugConsole& Get();

        // Printing API (to VS Output and to ImGui-console)
        void Info(const char* fmt, ...);
        void Warn(const char* fmt, ...);
        void Error(const char* fmt, ...);

        void InfoW(const wchar_t* fmt, ...);
        void WarnW(const wchar_t* fmt, ...);
        void ErrorW(const wchar_t* fmt, ...);

        void Raw(const std::string& s, Level lvl = Level::Info);
        void AppendFromHook(const char* utf8, Level lvl = Level::Info);
        void Clear();

        // Draw ImGui window
        void Draw(const char* title = "Console");

    private:
        DebugConsole() = default;
        DebugConsole(const DebugConsole&) = delete;
        DebugConsole& operator=(const DebugConsole&) = delete;

        struct Item {
            Level level{};
            std::string text;
        };

        std::deque<Item> m_items;
        size_t           m_capacity = 10000;
        bool             m_autoScroll = true;
        bool             m_wrap = true;
        bool             m_scrollToBottom = false;
        ImGuiTextFilter  m_filter;
        std::mutex       m_mutex;

        // helpers
        void add_formatted(Level lvl, const char* fmt, va_list args);
        void add_formatted(Level lvl, const char* fmt, ...);
        void add_formatted_w(Level lvl, const wchar_t* fmt, va_list args);
    };
}

#endif // IMGUI_LAYER_H