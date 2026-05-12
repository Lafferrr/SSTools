#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <securitybaseapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <functional>
#include <cwctype>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "scanner.hpp"
#include "detector.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

static constexpr int kTitleBarHeight = 40;

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND g_hwnd = nullptr;

static bool CreateDeviceD3D(HWND hwnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

struct LogEntry {
    std::string keyword;
    std::string category;
    std::string address;
    std::string regionType;
    std::string mappedFile;
    std::string rawValue;
    bool        fuzzy;
};

static MemoryScanner       g_scanner;
static StringDetector      g_detector;

static std::vector<LogEntry> g_log;
static std::mutex             g_logMutex;
static std::atomic<float>     g_progress{ 0.0f };
static std::atomic<bool>      g_scanning{ false };
static std::atomic<bool>      g_completed{ false };

static std::vector<DWORD> g_pids;
static int                g_selectedPid = 0;
static bool               g_autoScroll = true;

static float g_topPanelHeight = 195.0f;

static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static std::string AddrStr(uintptr_t addr) {
    std::ostringstream ss;
    ss << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << addr;
    return ss.str();
}

static const char* RegionName(RegionKind k) {
    switch (k) {
    case RegionKind::Image:   return "IMAGE";
    case RegionKind::Mapped:  return "MAPPED";
    case RegionKind::Private: return "PRIVATE";
    default:                  return "UNKNOWN";
    }
}

static void RefreshPids() {
    g_pids = g_scanner.FindByName(L"javaw.exe");
    g_selectedPid = 0;
}

static void StartScan() {
    if (g_scanning.load()) return;
    if (g_pids.empty()) return;

    DWORD pid = g_pids[g_selectedPid < (int)g_pids.size() ? g_selectedPid : 0];
    if (!g_scanner.Open(pid)) return;

    {
        std::lock_guard<std::mutex> lk(g_logMutex);
        g_log.clear();
    }
    g_progress = 0.0f;
    g_scanning = true;
    g_completed = false;

    ScanOptions opts;
    opts.scanImage = true;
    opts.scanMapped = true;
    opts.scanPrivate = true;
    opts.extractAscii = true;
    opts.extractUnicode = true;
    opts.minLength = 4;
    opts.chunkBytes = 4 * 1024 * 1024;

    StringCallback onStr = [](const ExtractedString& es) {
        std::wstring lowerFile = es.mappedFile;
        std::transform(lowerFile.begin(), lowerFile.end(), lowerFile.begin(), ::towlower);
        if (!lowerFile.empty()) {
            if (lowerFile.length() >= 4) {
                std::wstring ext4 = lowerFile.substr(lowerFile.length() - 4);
                if (ext4 == L".dll" || ext4 == L".ogg" || ext4 == L".png" || ext4 == L".json")
                    return;
            }
            if (lowerFile.length() >= 3) {
                std::wstring ext3 = lowerFile.substr(lowerFile.length() - 3);
                if (ext3 == L".pk")
                    return;
            }
        }

        if (es.text.find(L"step/") == 0 ||
            es.text.find(L"block/") == 0 ||
            es.text.find(L"enchant/") == 0 ||
            es.text.find(L"assets/") == 0)
            return;

        if (es.text.size() > 0 && es.text[0] == L'<') {
            if (es.text.find(L'>') != std::wstring::npos)
                return;
        }
        if (es.text.size() > 0 && es.text[0] == L'[') {
            if (es.text.find(L"]:") != std::wstring::npos)
                return;
        }

        auto hit = g_detector.Analyze(es.text);
        if (!hit) return;

        if (hit->matchedKeyword == L"Hitboxes") {
            std::wstring lowered = es.text;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);
            if (lowered.find(L"debug") != std::wstring::npos) return;
            if (lowered.find(L"feather.modules") != std::wstring::npos) return;
            if (lowered.find(L"feather.titles") != std::wstring::npos) return;
            if (lowered.find(L"hitboxes: shown") != std::wstring::npos) return;
            if (lowered.find(L"hitboxes: hidden") != std::wstring::npos) return;
            if (lowered.find(L"f3 + b = show hitboxes") != std::wstring::npos) return;
            if (lowered.find(L"litematica") != std::wstring::npos) return;
            if (lowered.find(L"schematic") != std::wstring::npos) return;
            if (lowered == L"show hitboxes") return;
            if (lowered == L"entity_hitboxes") return;
            if (lowered == L"https://twitter.com/wurst_imperium") return;
            if (lowered == L"contact.wurstimperium@gmail.com") return;
            if (lowered == L"show hitboxes!@") return;
            if (lowered == L"show hitboxes{") return;
        }
        else if (hit->matchedKeyword == L"meteor") {
            std::wstring lowered = es.text;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);
            if (lowered.find(L"meteorapp.com") != std::wstring::npos) return;
            if (lowered.find(L"meteor.exe") != std::wstring::npos) return;
            if (lowered.find(L"eu.meteorapp.com") != std::wstring::npos) return;
        }
        else if (hit->matchedKeyword == L"argon") {
            std::wstring lowered = es.text;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);
            if (lowered.find(L"jargon") != std::wstring::npos) return;
            if (lowered.find(L"argonauts") != std::wstring::npos) return;
            if (lowered.find(L"wi-zoom") != std::wstring::npos) return;
            if (lowered.find(L"wi + zoom") != std::wstring::npos) return;
            if (lowered.find(L"4604;florg") != std::wstring::npos) return;
        }

        LogEntry le;
        le.keyword = ToUtf8(hit->matchedKeyword);
        le.category = ToUtf8(hit->category);
        le.address = AddrStr(es.address);
        le.regionType = RegionName(es.kind);
        le.mappedFile = ToUtf8(es.mappedFile);
        le.rawValue = ToUtf8(es.text.size() > 128 ? es.text.substr(0, 128) + L"…" : es.text);
        le.fuzzy = hit->fuzzyMatch;

        std::lock_guard<std::mutex> lk(g_logMutex);
        g_log.push_back(std::move(le));
        };

    ProgressCallback onProg = [](float p) {
        g_progress = p;
        if (p >= 1.0f) {
            g_scanning = false;
            g_completed = true;
        }
        };

    g_scanner.BeginScan(opts, onStr, onProg);
}

static void StopScan() {
    g_scanner.StopScan();
    g_scanning = false;
    g_completed = false;
}

static void ApplyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 0.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.PopupRounding = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.TabRounding = 4.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = { 8.0f, 8.0f };
    s.FramePadding = { 8.0f, 5.0f };
    s.ItemSpacing = { 8.0f, 6.0f };
    s.ScrollbarSize = 12.0f;

    auto* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.090f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.040f, 0.047f, 0.070f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.063f, 0.090f, 1.00f);
    c[ImGuiCol_Border] = ImVec4(0.130f, 0.160f, 0.230f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.040f, 0.047f, 0.070f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.100f, 0.130f, 0.200f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.130f, 0.170f, 0.260f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.035f, 0.040f, 0.060f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.035f, 0.040f, 0.060f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.035f, 0.040f, 0.060f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.035f, 0.040f, 0.060f, 1.00f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.170f, 0.310f, 0.580f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.220f, 0.380f, 0.680f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.280f, 0.460f, 0.800f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.280f, 0.580f, 1.000f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.210f, 0.420f, 0.780f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.280f, 0.520f, 0.900f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.150f, 0.340f, 0.720f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.210f, 0.430f, 0.860f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.120f, 0.270f, 0.600f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.150f, 0.340f, 0.720f, 0.80f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.210f, 0.430f, 0.860f, 0.90f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.120f, 0.270f, 0.600f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.130f, 0.160f, 0.230f, 1.00f);
    c[ImGuiCol_Tab] = ImVec4(0.080f, 0.100f, 0.160f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.210f, 0.430f, 0.860f, 1.00f);
    c[ImGuiCol_TabActive] = ImVec4(0.150f, 0.340f, 0.720f, 1.00f);
    c[ImGuiCol_Text] = ImVec4(0.880f, 0.910f, 0.980f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.380f, 0.420f, 0.560f, 1.00f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.200f, 0.520f, 0.980f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.280f, 0.620f, 1.000f, 1.00f);
}

static void RenderUI() {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##main", nullptr, wf);
    ImGui::PopStyleVar();

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::BeginChild("##titlebar", ImVec2(ImGui::GetContentRegionAvail().x, (float)kTitleBarHeight), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetWindowFontScale(1.6f);
    float textHeight = ImGui::GetTextLineHeight();
    float titleY = (kTitleBarHeight - textHeight) * 0.5f;
    ImGui::SetCursorPosY(titleY);
    ImGui::Text(" Laffer's Strings Checker");

    float btnSize = 28.0f;
    float btnY = (kTitleBarHeight - btnSize) * 0.5f;
    ImGui::SetCursorPosY(btnY);
    ImGui::SameLine(ImGui::GetWindowWidth() - 68.0f);
    if (ImGui::Button("-", ImVec2(btnSize, btnSize))) {
        ShowWindow(g_hwnd, SW_MINIMIZE);
    }
    ImGui::SameLine();
    if (ImGui::Button("x", ImVec2(btnSize, btnSize))) {
        PostQuitMessage(0);
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(0)) {
        ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 1.0f));
    ImGui::Separator();
    ImGui::PopStyleVar();

    ImGui::BeginChild("##toppanel", ImVec2(0, g_topPanelHeight), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetWindowFontScale(1.5f);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Process:");
    ImGui::SameLine();
    float comboWidth = ImGui::GetContentRegionAvail().x - 100.0f;
    if (comboWidth < 120.0f) comboWidth = 120.0f;
    ImGui::SetNextItemWidth(comboWidth);
    std::string comboPreview = g_pids.empty() ? "(none found)" :
        ("PID " + std::to_string(g_pids[g_selectedPid < (int)g_pids.size() ? g_selectedPid : 0]));
    if (ImGui::BeginCombo("##pids", comboPreview.c_str())) {
        for (int i = 0; i < (int)g_pids.size(); ++i) {
            std::string label = "PID " + std::to_string(g_pids[i]);
            bool selected = (i == g_selectedPid);
            if (ImGui::Selectable(label.c_str(), selected)) g_selectedPid = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(90, 0))) RefreshPids();

    bool isScanning = g_scanning.load();
    bool isCompleted = g_completed.load();

    const char* statusLabel;
    ImVec4 statusColor;
    if (isScanning) {
        statusLabel = "Scanning...";
        statusColor = ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
    }
    else if (isCompleted) {
        statusLabel = "Completed";
        statusColor = ImVec4(0.25f, 0.88f, 0.45f, 1.0f);
    }
    else {
        statusLabel = "Ready";
        statusColor = ImVec4(0.55f, 0.60f, 0.75f, 1.0f);
    }
    ImGui::Spacing();
    ImGui::Text("Status:");
    ImGui::SameLine();
    ImGui::TextColored(statusColor, "%s", statusLabel);

    float progress = g_progress.load();
    char progressBuf[32];
    snprintf(progressBuf, sizeof(progressBuf), "%.1f%%", progress * 100.0f);
    ImGui::ProgressBar(progress, ImVec2(-1, 18.0f), progressBuf);

    ImGui::Spacing();
    ImGui::Spacing();

    if (isScanning) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.08f, 0.08f, 1.0f));
        if (ImGui::Button("  Stop Scan  ", ImVec2(140, 32))) StopScan();
        ImGui::PopStyleColor(3);
    }
    else {
        ImGui::BeginDisabled(g_pids.empty());
        if (ImGui::Button("  Start Scan  ", ImVec2(140, 32))) StartScan();
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Checkbox("Auto-scroll", &g_autoScroll);

    size_t cnt;
    {
        std::lock_guard<std::mutex> lk(g_logMutex);
        cnt = g_log.size();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu Results Found", cnt);

    float clearBtnWidth = 60.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - clearBtnWidth - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Clear", ImVec2(clearBtnWidth, 0))) {
        std::lock_guard<std::mutex> lk(g_logMutex);
        g_log.clear();
        g_completed = false;
        g_progress = 0.0f;
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::Button("##splitter", ImVec2(ImGui::GetContentRegionAvail().x, 4.0f));
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.y;
        if (delta != 0.0f) {
            g_topPanelHeight += delta;
            if (g_topPanelHeight < 80.0f) g_topPanelHeight = 80.0f;
            float maxHeight = io.DisplaySize.y - kTitleBarHeight - 60.0f;
            if (g_topPanelHeight > maxHeight) g_topPanelHeight = maxHeight;
        }
    }

    ImGui::BeginChild("##results", ImVec2(0, 0), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::SetWindowFontScale(1.5f);

    std::vector<LogEntry> snapshot;
    {
        std::lock_guard<std::mutex> lk(g_logMutex);
        snapshot = g_log;
    }
    std::sort(snapshot.begin(), snapshot.end(),
        [](const LogEntry& a, const LogEntry& b) {
            return a.keyword < b.keyword;
        });

    ImGuiTableFlags tf =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("##results", 3, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Keyword", ImGuiTableColumnFlags_WidthFixed, 240.0f);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 185.0f);
        ImGui::TableSetupColumn("Raw Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)snapshot.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& le = snapshot[row];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImVec4 kwColor = le.fuzzy
                    ? ImVec4(1.0f, 0.82f, 0.28f, 1.0f)
                    : ImVec4(0.35f, 0.72f, 1.0f, 1.0f);
                ImGui::TextColored(kwColor, "%s", le.keyword.c_str());
                if (le.fuzzy && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fuzzy / obfuscated match");

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", le.category.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(le.rawValue.c_str());
            }
        }
        clipper.End();

        if (g_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndTable();
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lp;
            if (IsZoomed(hwnd)) {
                HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                GetMonitorInfo(hmon, &mi);
                params->rgrc[0] = mi.rcWork;
            }
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (pt.y >= 0 && pt.y < kTitleBarHeight) {
            if (pt.x > rc.right - 120) return HTCLIENT;
            if (pt.x < 40) return HTCLIENT;
            return HTCAPTION;
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 640;
        mmi->ptMinTrackSize.y = 400;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(mon, &mi);
        mmi->ptMaxPosition.x = mi.rcWork.left;
        mmi->ptMaxPosition.y = mi.rcWork.top;
        mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
        mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        return 0;
    }
    case WM_SIZE:
        if (g_device && wp != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION,
        &sd, &g_swapChain, &g_device, &fl, &g_ctx);

    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    BOOL isAdmin = FALSE;
    PSID adminGroup;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    if (!isAdmin) {
        WCHAR path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        ShellExecuteW(NULL, L"runas", path, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }

    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"StringsWindow";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    g_hwnd = CreateWindowExW(
        0, L"StringsWindow", L"Laffer's Strings Checker",
        style,
        100, 100, 1280, 820,
        nullptr, nullptr, hInst, nullptr);

    BOOL useDark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20, &useDark, sizeof(useDark));

    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInst);
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImFontConfig fc;
    fc.OversampleH = 2;
    fc.OversampleV = 2;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &fc);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();

    ApplyTheme();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_ctx);

    RefreshPids();

    const ImVec4 clearColor = ImVec4(0.055f, 0.063f, 0.090f, 1.0f);
    bool running = true;

    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        const float cc[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    StopScan();
    g_scanner.Close();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);

    return 0;
}