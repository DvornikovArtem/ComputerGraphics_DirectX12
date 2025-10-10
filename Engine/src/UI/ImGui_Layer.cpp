// ImGui_Layer.cpp

#include <Engine/UI/ImGui_Layer.h>
#include <cassert>
#include <sstream>

namespace Engine::UI
{
    ImGuiLayer::~ImGuiLayer() { Shutdown(); }

    void ImGuiLayer::Initialize(const Desc& d) {
        assert(d.hwnd && d.device && d.cmdQueue);
        cfg = d;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


        // Enable DPI awareness for fonts
        io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
        // Enable DPI awareness for windows/viewports
        io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;


        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        //ImGui_ImplWin32_EnableDpiAwareness();

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = cfg.srvHeapCapacity;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        d.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mSrvHeap));

        mDescriptorSize = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        ImGui_ImplWin32_Init(cfg.hwnd);

        ImGui_ImplDX12_InitInfo init = {};
        init.Device = cfg.device;
        init.CommandQueue = cfg.cmdQueue;
        init.NumFramesInFlight = cfg.framesInFlight;
        init.RTVFormat = cfg.rtvFormat;
        init.DSVFormat = DXGI_FORMAT_UNKNOWN;
        init.SrvDescriptorHeap = mSrvHeap.Get();
        
        init.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
            auto* layer = reinterpret_cast<ImGuiLayer*>(info->UserData);
            layer->AllocSrv(*out_cpu, *out_gpu);
            };
        init.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};

        init.UserData = this;

        ImGui_ImplDX12_Init(&init);


        // Initial DPI for the main window
        // Get the actual window scale (e.g., 1.0 for 96 DPI, 1.75 for 168 DPI)
        const float initialDPI = ImGui_ImplWin32_GetDpiScaleForHwnd(cfg.hwnd);

        // Print initial DPI scale
        std::ostringstream oss;
        oss << "DPI scale: " << initialDPI << "\n";
        OutputDebugStringA(oss.str().c_str());

        // Rebuild the font atlas for the current DPI, reset global scales to 1.0
        // (i.e. don't "stretch" the existing atlas, but generate a pixel-perfect texture instead)
        OnDpiChanged(initialDPI);

        mInitialized = true;
    }



    /**
     * @brief Scales the entire ImGui interface to match a specific DPI.
     *
     * Call this when you need to rescale the UI for a given DPI.
     *
     * @param newDpi New DPI scale, calculated as DPI / 96.
     */
    void ImGuiLayer::OnDpiChanged(float newDpi) const
    {
        if (!mInitialized) return;


        // Get global ImGui settings
        ImGuiIO& io = ImGui::GetIO();
        // Get the current UI style (paddings, button sizes, line thicknesses, etc.)
        ImGuiStyle& style = ImGui::GetStyle();


        // Rebuild fonts for the new DPI
        
        // Clear old fonts
        io.Fonts->Clear();

        // New font configuration
        ImFontConfig cfg{};
        // Base font size in pixels
        const float base_px = 13.0f;
        // Set a new font size proportional to the DPI
        cfg.SizePixels = base_px * newDpi;

        // Add the default font
        io.Fonts->AddFontDefault(&cfg);
        // Rebuild font atlases
        io.Fonts->Build();


        // Scale all visual elements
        const float scale = newDpi;
        // Scale all UI element sizes by computing the relative factor
        // (for example, if the old DPI was 1.0 and the new one is 1.5, scale all sizes by x1.5).
        style.ScaleAllSizes(scale / io.DisplayFramebufferScale.x);
        // Store the new DPI value in ImGui so it knows the current UI scaling
        io.DisplayFramebufferScale = ImVec2(scale, scale);


        std::ostringstream oss;
        oss << "[ImGuiLayer] DPI changed -> " << newDpi << "\n";
        OutputDebugStringA(oss.str().c_str());
    }



    void ImGuiLayer::Shutdown() {
        if (!mInitialized) return;
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        mSrvHeap.Reset();
        mInitialized = false;
    }

    void ImGuiLayer::NewFrame() {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (m_pendingLayoutOpBegin.kind == LayoutOpKind::Load) {
            load_layout_now(m_pendingLayoutOpBegin.path);
            m_pendingLayoutOpBegin = {};
        }
    }

    void ImGuiLayer::RenderDrawData(ID3D12GraphicsCommandList* cmdList) {
        ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault(nullptr, (void*)cmdList);
        }

        if (m_pendingLayoutOpEnd.kind == LayoutOpKind::Save) {
            save_layout_now(m_pendingLayoutOpEnd.path);
            m_pendingLayoutOpEnd = {};
        }
    }

    void ImGuiLayer::AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE& outCPU, D3D12_GPU_DESCRIPTOR_HANDLE& outGPU) {
        auto cpuStart = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
        auto gpuStart = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
        cpuStart.ptr += SIZE_T(mSrvAllocIndex) * mDescriptorSize;
        gpuStart.ptr += UINT64(mSrvAllocIndex) * mDescriptorSize;
        outCPU = cpuStart;
        outGPU = gpuStart;
        ++mSrvAllocIndex;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ImGuiLayer::CreateTextureSRV(ID3D12Resource* tex, DXGI_FORMAT fmt) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        AllocSrv(cpu, gpu);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = fmt;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        cfg.device->CreateShaderResourceView(tex, &srv, cpu);
        return gpu;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ImGuiLayer::CreateOrOverwriteTextureSRV(ID3D12Resource* tex, DXGI_FORMAT fmt, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = fmt;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE dstCPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE dstGPU{};

        if (gpuHandle.ptr != 0) {
            auto cpuBase = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
            auto gpuBase = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
            const UINT stride = mDescriptorSize;
            const UINT64 offset = (gpuHandle.ptr - gpuBase.ptr) / UINT64(stride);
            dstCPU = cpuBase;
            dstCPU.ptr += SIZE_T(offset) * stride;
            dstGPU = gpuHandle;
        }
        else {
            AllocSrv(dstCPU, dstGPU);
        }

        cfg.device->CreateShaderResourceView(tex, &srv, dstCPU);
        return dstGPU;
    }

    void ImGuiLayer::CopySrvIntoSlot(D3D12_GPU_DESCRIPTOR_HANDLE dstGpuHandle, D3D12_CPU_DESCRIPTOR_HANDLE srcCpuHandle)
    {
        if (dstGpuHandle.ptr == 0 || srcCpuHandle.ptr == 0) return;

        auto cpuBase = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
        auto gpuBase = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
        const UINT stride = mDescriptorSize;

        const UINT64 offset = (dstGpuHandle.ptr - gpuBase.ptr) / UINT64(stride);
        D3D12_CPU_DESCRIPTOR_HANDLE dstCPU = cpuBase;
        dstCPU.ptr += SIZE_T(offset) * stride;

        cfg.device->CopyDescriptorsSimple(1, dstCPU, srcCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ImTextureID ImGuiLayer::GetOrCreateImTextureIDFromSrvCPU(D3D12_CPU_DESCRIPTOR_HANDLE srcSrvCPU)
    {
        if (srcSrvCPU.ptr == 0) return ImTextureID{};

        const SIZE_T key = srcSrvCPU.ptr;
        auto it = mSrvCpuToGpu.find(key);
        if (it != mSrvCpuToGpu.end())
            return (ImTextureID)it->second.ptr;

        D3D12_CPU_DESCRIPTOR_HANDLE dstCPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE dstGPU{};
        AllocSrv(dstCPU, dstGPU);

        cfg.device->CopyDescriptorsSimple(1, dstCPU, srcSrvCPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        mSrvCpuToGpu.emplace(key, dstGPU);
        return (ImTextureID)dstGPU.ptr;
    }
    

    void ImGuiLayer::DrawTextureGridFixedSize_ImTexID(const char* windowTitle, const std::vector<std::pair<ImTextureID, std::string>>& textures, int rows, int cols, ImVec2 tileSize, ImVec2 uv0, ImVec2 uv1, float cellPadding, bool showLabels)
    {
        if (!windowTitle) return;

        const int n = (int)textures.size();
        if (rows <= 0 || cols <= 0) {
            if (n == 0) return;
            cols = (int)std::ceil(std::sqrt((float)n));
            rows = (int)std::ceil(n / (float)cols);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
        if (ImGui::Begin(windowTitle))
        {
            ImGui::BeginChild("##tex_grid_fixed_scroll",
                ImVec2(0, 0), false,
                ImGuiWindowFlags_AlwaysVerticalScrollbar |
                ImGuiWindowFlags_AlwaysHorizontalScrollbar);

            const ImGuiTableFlags tf = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
            if (ImGui::BeginTable("##tex_grid_fixed_id", cols, tf))
            {
                int idx = 0;
                for (int r = 0; r < rows; ++r)
                {
                    ImGui::TableNextRow();
                    for (int c = 0; c < cols; ++c)
                    {
                        ImGui::TableSetColumnIndex(c);
                        if (idx >= n) { ImGui::Dummy(tileSize); continue; }

                        const ImTextureID id = textures[idx].first;
                        const std::string& name = textures[idx].second;
                        const bool valid = (id != 0);

                        if (cellPadding > 0.0f && c > 0)
                            ImGui::Dummy(ImVec2(cellPadding, 1.0f));

                        if (valid)  ImGui::Image(id, tileSize, uv0, uv1);
                        else        ImGui::Dummy(tileSize);

                        if (valid && ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            const float maxW = 256.0f;
                            const float aspect = (tileSize.x > 0.0f) ? (tileSize.y / tileSize.x) : 1.0f;
                            ImGui::Image(id, ImVec2(maxW, maxW * aspect), uv0, uv1);
                            if (showLabels) { ImGui::Separator(); ImGui::Text("%s", name.c_str()); }
                            ImGui::EndTooltip();
                        }

                        if (showLabels) {
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tileSize.x);
                            ImGui::Text("%s", name.c_str());
                            ImGui::PopTextWrapPos();
                        }

                        ++idx;
                    }
                    if (r < rows - 1 && cellPadding > 0.0f)
                        ImGui::Dummy(ImVec2(1.0f, cellPadding));
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }


    bool ImGuiLayer::load_layout_now(const std::filesystem::path& file) {
        if (file.empty()) return false;
        ImGui::LoadIniSettingsFromDisk(file.string().c_str());
        return true;
    }

    bool ImGuiLayer::save_layout_now(const std::filesystem::path& file) const {
        if (file.empty()) return false;
        ImGui::SaveIniSettingsToDisk(file.string().c_str());
        return true;
    }

    void ImGuiLayer::queue_load_layout(const std::filesystem::path& file) {
        m_pendingLayoutOpBegin = { LayoutOpKind::Load, file };
    }

    void ImGuiLayer::queue_save_layout(const std::filesystem::path& file) {
        m_pendingLayoutOpEnd = { LayoutOpKind::Save, file };
    }

    void ImGuiLayer::DrawBuiltins()
    {
        if (mShowConsole)
            Engine::UI::DebugConsole::Get().Draw("Console");
    }
}


#include <windows.h>
#include <chrono>
#include <iomanip>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace Engine::UI {
    thread_local bool g_ConsoleEchoGuard = false;
}


inline std::string vformat(const char* fmt, va_list args) {
    va_list copy; va_copy(copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (len <= 0) return {};
    std::string out(len, '\0');
    std::vsnprintf(out.data(), out.size() + 1, fmt, args);
       
    return out;
}
inline std::string from_wide_v(const wchar_t* fmt, va_list args) {
    va_list copy; va_copy(copy, args);
    int lenW = _vscwprintf(fmt, copy);
    va_end(copy);
    if (lenW <= 0) return {};
    std::wstring w(lenW, L'\0');
    _vsnwprintf_s(w.data(), w.size() + 1, _TRUNCATE, fmt, args);

    int lenU8 = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u8(lenU8 ? lenU8 - 1 : 0, '\0');
    if (lenU8 > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u8.data(), lenU8, nullptr, nullptr);
    return u8;
}

inline const ImVec4& color_for(Engine::UI::DebugConsole::Level lvl) {
    static const ImVec4 cInfo(0.85f, 0.85f, 0.85f, 1.0f);
    static const ImVec4 cWarn(1.00f, 0.90f, 0.55f, 1.0f);
    static const ImVec4 cError(1.00f, 0.50f, 0.50f, 1.0f);
    switch (lvl) {
        case Engine::UI::DebugConsole::Level::Warn: return cWarn;
        case Engine::UI::DebugConsole::Level::Error: return cError;
        default: return cInfo;
    }
}

inline const char* tag_for(Engine::UI::DebugConsole::Level lvl) {
    switch (lvl) {
    case Engine::UI::DebugConsole::Level::Warn: return "[Warning] ";
    case Engine::UI::DebugConsole::Level::Error: return "[Error] ";
    default: return "[Information] ";
    }
}

inline std::string timestamp_now() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto sec = time_point_cast<seconds>(tp);
    auto ms = duration_cast<milliseconds>(tp - sec).count();

    std::time_t t = system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_s(&tm, &t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

namespace Engine::UI {

    DebugConsole& DebugConsole::Get() {
        static DebugConsole inst;
        return inst;
    }

    void DebugConsole::add_formatted(Level lvl, const char* fmt, va_list args) {
        std::string msg = vformat(fmt, args);
        if (!msg.empty() && msg.back() == '\n') msg.pop_back();

        std::string line;
        line.reserve(msg.size() + 32);
        line += timestamp_now();
        line += ' ';
        line += tag_for(lvl);
        line += msg;

        Engine::UI::g_ConsoleEchoGuard = true;
        // Call the original global WinAPI function OutputDebugStringA, not a locally overridden version with the same name
        ::OutputDebugStringA((line + "\n").c_str());
        Engine::UI::g_ConsoleEchoGuard = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_items.push_back({ lvl, std::move(line) });
            if (m_items.size() > m_capacity) m_items.pop_front();
            //m_scrollToBottom = true;
        }
    }

    void DebugConsole::add_formatted(Level lvl, const char* fmt, ...) {
        va_list a;
        va_start(a, fmt);
        add_formatted(lvl, fmt, a);
        va_end(a);
    }

    void DebugConsole::add_formatted_w(Level lvl, const wchar_t* fmt, va_list args) {
        std::string u8 = from_wide_v(fmt, args);
        if (u8.empty()) return;

        add_formatted(lvl, "%s", u8.c_str());
    }

    void DebugConsole::Info(const char* fmt, ...) { va_list a; va_start(a, fmt); add_formatted(Level::Info, fmt, a); va_end(a); }
    void DebugConsole::Warn(const char* fmt, ...) { va_list a; va_start(a, fmt); add_formatted(Level::Warn, fmt, a); va_end(a); }
    void DebugConsole::Error(const char* fmt, ...) { va_list a; va_start(a, fmt); add_formatted(Level::Error, fmt, a); va_end(a); }

    void DebugConsole::InfoW(const wchar_t* fmt, ...) { va_list a; va_start(a, fmt); add_formatted_w(Level::Info, fmt, a); va_end(a); }
    void DebugConsole::WarnW(const wchar_t* fmt, ...) { va_list a; va_start(a, fmt); add_formatted_w(Level::Warn, fmt, a); va_end(a); }
    void DebugConsole::ErrorW(const wchar_t* fmt, ...) { va_list a; va_start(a, fmt); add_formatted_w(Level::Error, fmt, a); va_end(a); }

    void DebugConsole::Raw(const std::string& s, Level lvl) { Info("%s", s.c_str()); }

    void Engine::UI::DebugConsole::AppendFromHook(const char* utf8, Level lvl)
    {
        std::string msg = utf8 ? utf8 : "";
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();

        std::string line;
        line.reserve(msg.size() + 32);
        line += timestamp_now();
        line += ' ';
        line += tag_for(lvl);
        line += msg;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.push_back({ lvl, std::move(line) });
        if (m_items.size() > m_capacity) m_items.pop_front();
    }

    void DebugConsole::Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear();
    }


    void DebugConsole::Draw(const char* title) {
        if (!ImGui::Begin(title)) { ImGui::End(); return; }

        if (ImGui::Button("Clear")) { Clear(); }
        ImGui::SameLine();
        if (ImGui::Button("Copy All")) {
            std::string all;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                all.reserve(1024);
                for (auto& it : m_items) { all += it.text; all += '\n'; }
            }
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::SameLine();
        m_filter.Draw("Filter", 200.0f);

        if (ImGui::BeginPopupContextItem("console_ctx")) {
            static bool softWrapColor = false;
            static bool softWrapRaw = false;
            ImGui::MenuItem("Auto-scroll (near bottom)", nullptr, &m_autoScroll);
            ImGui::Separator();
            ImGui::MenuItem("Soft wrap (Color view)", nullptr, &softWrapColor);
            ImGui::MenuItem("Soft wrap (Raw view)", nullptr, &softWrapRaw);
            if (ImGui::MenuItem("Scroll to bottom")) m_scrollToBottom = true;
            ImGui::EndPopup();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("console_ctx");

        ImGui::Separator();

        std::string bigBuffer;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& it : m_items) {
                if (!m_filter.PassFilter(it.text.c_str())) continue;
                bigBuffer += it.text;
                bigBuffer += '\n';
            }
        }

        bool wantAutoScroll = false;
        float prevMaxY = ImGui::GetScrollMaxY();
        float prevY = ImGui::GetScrollY();
        if (prevMaxY <= 0.0f || (prevMaxY - prevY) < 2.0f) {
            wantAutoScroll = true;
        }

        if (ImGui::BeginTabBar("##console_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {

            if (ImGui::BeginTabItem("Color")) {
                ImGuiWindowFlags childFlags =
                    ImGuiWindowFlags_AlwaysVerticalScrollbar |
                    ImGuiWindowFlags_AlwaysHorizontalScrollbar;

                ImGui::BeginChild("##console_color", ImVec2(0, 0), false, childFlags);

                if (/*softWrapColor*/ false) {
                }

                ImGuiListClipper clipper;
                int lineCount = 0;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (const auto& it : m_items) {
                        if (m_filter.PassFilter(it.text.c_str())) ++lineCount;
                    }
                }
                clipper.Begin(lineCount);

                while (clipper.Step()) {
                    int idx = 0;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (const auto& it : m_items) {
                        if (!m_filter.PassFilter(it.text.c_str())) continue;
                        if (idx >= clipper.DisplayEnd) break;
                        if (idx >= clipper.DisplayStart) {
                            ImGui::PushStyleColor(ImGuiCol_Text, color_for(it.level));
                            ImGui::TextUnformatted(it.text.c_str());
                            ImGui::PopStyleColor();
                        }
                        ++idx;
                    }
                }

                if (/*softWrapColor*/ false) {
                    // ImGui::PopTextWrapPos();
                }

                if ((m_autoScroll && wantAutoScroll) || m_scrollToBottom) {
                    ImGui::SetScrollHereY(1.0f);
                    m_scrollToBottom = false;
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Raw")) {
                ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo;
                ImVec2 size = ImVec2(-FLT_MIN, -FLT_MIN);
                ImGui::InputTextMultiline("##console_text_raw",
                    bigBuffer.data(),
                    bigBuffer.size() + 1,
                    size,
                    flags);

                if ((m_autoScroll && wantAutoScroll) || m_scrollToBottom) {
                    ImGui::SetScrollHereY(1.0f);
                    m_scrollToBottom = false;
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

}