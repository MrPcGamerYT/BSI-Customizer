// BS_THEME_CHANGER - Exact Rebuild - C++ ImGui Version
// Original: PE32+ x64 GUI, ThemeStudio, BlueStacks Theme Studio, requireAdministrator
// ImGui 1.91.9b + D3D11 + zlib 1.3.2
// This rebuild replicates exact same strings, methods, and UI IDs

#include "theme.h"
#include "patcher.h"
#include <iostream>
#include <filesystem>
#include <map>
#include <string>

// ImGui includes (same version as original 1.91.9b)
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

// Same UI IDs as original binary
#define ID_YT_TOP "yt_top"
#define ID_DC_TOP "dc_top"
#define ID_MIN_TOP "min_top"
#define ID_CLOSE_TOP "close_top"
#define ID_PATH "##path"
#define ID_BROWSE "Browse"
#define ID_PRESET "##preset"
#define ID_PRESET_ANIM "preset_anim"
#define ID_FILTER "##filter"
#define ID_COLORLIST "colorList"
#define ID_ROW "##row"
#define ID_ROW_ANIM "row_anim"
#define ID_PICKBOX "##pickbox"
#define ID_TINY_PICKER "##tinyPicker"
#define ID_VALUE "##value"
#define ID_MODAL "##modal"

// Global state (same as original)
static ThemeRes g_themeRes;
static std::string g_exePath;
static std::string g_statusMessage = STR_MSG_SELECT_BEGIN;
static std::string g_errorMessage;
static bool g_showModal = false;
static std::string g_modalMessage;
static std::vector<AutoPreset> g_presets;
static int g_selectedPreset = -1;
static char g_filterBuffer[256] = "";
static std::map<std::string, std::string> g_editedColors;
static bool g_themeLoaded = false;

// Fonts - same as original
static const char* FONT_SFPRO = "\\Fonts\\SFProDisplay-Regular.ttf";
static const char* FONT_SEGOE = "\\Fonts\\segoeui.ttf";

// Forward declarations
bool LoadHDPlayerFile(const std::string& path);
void RenderUI();
void ShowErrorModal(const std::string& msg);
std::string OpenFileDialog();

// Win32 + D3D11 setup (same as original uses D3D11CreateDevice, D3DCompile, DwmEnableBlurBehindWindow etc)
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main entry - same as original uses WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Load presets - pure black minimal, Amethyst high contrast purple
    BlueStacksPatcher patcher;
    g_presets = patcher.LoadPresets();

    // Register window class
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ThemeStudio", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"BlueStacks Theme Studio - THEME CHANGER", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize D3D11
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup ImGui 1.91.9b
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // no ini file, same as original?

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Load fonts - try SF Pro Display and Segoe UI as in original
    // Original strings: \Fonts\SFProDisplay-Regular.ttf and \Fonts\segoeui.ttf
    // We'll attempt to load from system
    std::string sfProPath = "C:\\Windows\\Fonts\\segoeui.ttf"; // fallback
    if (std::filesystem::exists("C:\\Windows\\Fonts\\SFProDisplay-Regular.ttf")) sfProPath = "C:\\Windows\\Fonts\\SFProDisplay-Regular.ttf";
    io.Fonts->AddFontFromFileTTF(sfProPath.c_str(), 16.0f);

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        const float clear_color[4] = { 0.08f, 0.08f, 0.08f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

void RenderUI() {
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Top bar - yt_top, dc_top, min_top, close_top (same as original)
    ImGui::BeginGroup();
    ImGui::Text(STR_THEME_CHANGER);
    ImGui::SameLine();
    ImGui::TextDisabled(STR_SUBTITLE);
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    if (ImGui::Button("YT##yt_top")) {
        ShellExecuteW(nullptr, L"open", L"https://youtube.com/@Romulus_FF", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("DC##dc_top")) {
        ShellExecuteW(nullptr, L"open", L"https://dsc.gg/romulus", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("_##min_top")) {
        ShowWindow(FindWindowW(L"ThemeStudio", nullptr), SW_MINIMIZE);
    }
    ImGui::SameLine();
    if (ImGui::Button("X##close_top")) {
        PostQuitMessage(0);
    }
    ImGui::EndGroup();

    ImGui::Separator();

    // Left panel - file picker and presets
    ImGui::BeginChild("leftPanel", ImVec2(350, 0), true);

    ImGui::Text("HD-Player.exe");
    static char pathBuf[512] = "";
    ImGui::InputText(ID_PATH, pathBuf, sizeof(pathBuf));
    ImGui::SameLine();
    if (ImGui::Button(ID_BROWSE "##browse")) {
        std::string selected = OpenFileDialog();
        if (!selected.empty()) {
            strcpy_s(pathBuf, selected.c_str());
            g_exePath = selected;
            std::string err;
            g_themeRes = BlueStacksPatcher().LoadFile(g_exePath, err);
            if (!g_themeRes.found) {
                g_statusMessage = err; // Theme was not found in this file. / Cannot read file.
                g_themeLoaded = false;
                ShowErrorModal(err);
            } else {
                BlueStacksPatcher p;
                std::string derr;
                p.DecompressTheme(g_themeRes, derr);
                p.ParseColors(g_themeRes);
                g_themeLoaded = true;
                g_statusMessage = STR_MSG_LOADED; // Loaded. Choose preset or edit manually.
            }
        }
    }

    if (!g_themeLoaded) {
        ImGui::TextDisabled(STR_MSG_SELECT_BEGIN);
    }

    ImGui::Separator();
    ImGui::Text(STR_THEME_PRESETS);

    // Filter - search every color
    ImGui::InputText(ID_FILTER, g_filterBuffer, sizeof(g_filterBuffer), ImGuiInputTextFlags_::ImGuiInputTextFlags_None);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("search every color");

    // Preset list
    for (int i = 0; i < g_presets.size(); ++i) {
        bool selected = (g_selectedPreset == i);
        std::string label = g_presets[i].name + " - " + g_presets[i].desc + "##preset";
        if (ImGui::Selectable(label.c_str(), selected)) {
            g_selectedPreset = i;
            BlueStacksPatcher p;
            p.ApplyPreset(g_themeRes, g_presets[i]);
            // Update edited colors
            for (auto& c : g_themeRes.colors) {
                g_editedColors[c.name] = c.currentHex;
            }
        }
        if (selected) {
            // preset_anim
            ImGui::SetItemDefaultFocus();
        }
    }

    ImGui::Separator();
    ImGui::Text("Colors");
    ImGui::BeginChild(ID_COLORLIST, ImVec2(0, 0), false);

    int shown = 0;
    for (auto& color : g_themeRes.colors) {
        // Filter
        if (strlen(g_filterBuffer) > 0) {
            std::string filterLower = g_filterBuffer;
            std::string nameLower = color.name;
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find(filterLower) == std::string::npos && color.currentHex.find(filterLower) == std::string::npos) {
                continue;
            }
        }

        ImGui::PushID(color.name.c_str());
        // Color box
        ImVec4 col = ImVec4(1,1,1,1);
        // Parse hex to ImVec4
        if (color.currentHex.size() >= 7) {
            std::string hex = color.currentHex;
            if (hex[0] == '#') hex = hex.substr(1);
            if (hex.size() >= 6) {
                int r = std::stoi(hex.substr(0,2), nullptr, 16);
                int g = std::stoi(hex.substr(2,2), nullptr, 16);
                int b = std::stoi(hex.substr(4,2), nullptr, 16);
                col = ImVec4(r/255.0f, g/255.0f, b/255.0f, 1.0f);
            }
        }

        if (ImGui::ColorButton(ID_ROW, col, ImGuiColorEditFlags_NoTooltip, ImVec2(20,20))) {
            ImGui::OpenPopup(ID_PICKBOX);
        }
        ImGui::SameLine();
        ImGui::Text("%s: %s", color.name.c_str(), color.currentHex.c_str());

        // Tiny picker popup - ##pickbox ##tinyPicker
        if (ImGui::BeginPopup(ID_PICKBOX)) {
            ImGui::Text("Color##pickbox");
            static float pickerCol[4] = {1,1,1,1};
            pickerCol[0] = col.x; pickerCol[1] = col.y; pickerCol[2] = col.z;
            if (ImGui::ColorPicker4(ID_TINY_PICKER, pickerCol, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                // Update
                char newHex[16];
                sprintf_s(newHex, "#%02X%02X%02X", (int)(pickerCol[0]*255), (int)(pickerCol[1]*255), (int)(pickerCol[2]*255));
                color.currentHex = newHex;
                g_editedColors[color.name] = newHex;
            }
            if (ImGui::Button("Done##tiny_done")) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Reset##entry_reset")) {
                color.currentHex = color.originalHex;
                g_editedColors[color.name] = color.originalHex;
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply##entry_use")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        shown++;
        // row_anim - animate rows
        if (shown % 2 == 0) ImGui::Separator();
    }

    if (shown == 0) {
        ImGui::TextDisabled("No colors loaded/matched");
    }

    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel - PATCH PLAN
    ImGui::BeginChild("rightPanel", ImVec2(0,0), true);
    ImGui::Text("PATCH PLAN");

    if (g_selectedPreset >= 0) {
        ImGui::Text("Preset: %s", g_presets[g_selectedPreset].name.c_str());
    } else {
        ImGui::Text("Preset: %s", "  |  select + patch");
    }

    if (!g_themeLoaded) {
        ImGui::TextDisabled("select HD-Player.exe first");
    } else {
        ImGui::TextDisabled("QML colors, hover states, sliders, toggles, SVGs.");
    }

    ImGui::Separator();

    // Show edited colors count
    ImGui::Text("Original: %zu colors", g_themeRes.colors.size());
    ImGui::Text("Edited: %zu", g_editedColors.size());

    ImGui::Separator();

    // Patch button
    if (ImGui::Button("Patch##patch", ImVec2(100, 40))) {
        if (!g_themeLoaded) {
            ShowErrorModal(STR_MSG_SELECT_BEGIN);
        } else {
            BlueStacksPatcher patcher;
            // Build patched QML
            std::map<std::string, std::string> overrides = g_editedColors;
            std::string patchedQml = patcher.BuildPatchedQML(g_themeRes, overrides);

            std::vector<uint8_t> newCompressed;
            std::string compErr;
            if (!patcher.CompressTheme(patchedQml, newCompressed, compErr)) {
                ShowErrorModal(compErr); // Theme compression failed.
            } else {
                std::string outMsg, outErr;
                const AutoPreset* presetPtr = (g_selectedPreset >= 0) ? &g_presets[g_selectedPreset] : nullptr;
                bool ok = patcher.PatchFile(g_exePath, g_themeRes, newCompressed, presetPtr, outMsg, outErr);
                if (!ok) {
                    if (outErr == STR_ERR_NO_ROOM) {
                        ShowErrorModal(outErr);
                    } else {
                        ShowErrorModal(outErr.empty() ? STR_MSG_PATCH_FAILED : outErr);
                    }
                } else {
                    // Verify
                    std::string verifyErr;
                    if (!patcher.VerifyPatched(g_exePath, newCompressed, verifyErr)) {
                        ShowErrorModal(verifyErr); // Patched theme verify failed.
                    } else {
                        g_statusMessage = outMsg + " " + STR_MSG_DONE_REPATCH;
                        g_showModal = true;
                        g_modalMessage = outMsg + "\n" + STR_MSG_DONE_REPATCH; // Done. Full theme table patched. You can repatch again without backup.
                    }
                }
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Restore##restore", ImVec2(100,40))) {
        BlueStacksPatcher patcher;
        std::string outMsg;
        bool ok = patcher.RestoreBackup(g_exePath, outMsg);
        if (!ok) {
            ShowErrorModal(outMsg); // No backup found. / Restore failed.
        } else {
            g_statusMessage = outMsg; // Restored from backup.
            g_showModal = true;
            g_modalMessage = outMsg;
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", g_statusMessage.c_str());

    ImGui::EndChild();

    // Modal - ##modal %s OK ok Patch failed Restore failed
    if (g_showModal) {
        ImGui::OpenPopup(ID_MODAL);
    }
    if (ImGui::BeginPopupModal(ID_MODAL, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", g_modalMessage.c_str());
        if (ImGui::Button("OK##ok", ImVec2(120,0))) {
            ImGui::CloseCurrentPopup();
            g_showModal = false;
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

std::string OpenFileDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "EXE files\0*.exe\0All files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "exe";
    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

void ShowErrorModal(const std::string& msg) {
    g_showModal = true;
    g_modalMessage = msg;
}

// D3D11 helpers (same as original uses D3D11CreateDevice etc)
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
