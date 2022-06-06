#include <Windows.h>
#include <iostream>
#include <Psapi.h>
#include <TlHelp32.h>
#include "MinHook.h"
#include "kiero.h"
#include <d3d11.h>
#include <dxgi.h>
#include <sstream>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#pragma comment(lib, "MinHook.lib")
#include "json.hpp"
#include <fstream>
//#include "xorstr.h"

using namespace nlohmann;

typedef HRESULT(__stdcall* Present) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef uintptr_t PTR;

Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;

void InitImGui()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Arial.ttf", 12, NULL, io.Fonts->GetGlyphRangesCyrillic());
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;

    ImGuiStyle* style = &ImGui::GetStyle();

    style->WindowPadding = ImVec2(8.f, 8.f);
    style->WindowRounding = 5.0f;
    style->FramePadding = ImVec2(6.f, 6.f);
    style->FrameRounding = 4.0f;
    style->ItemSpacing = ImVec2(6, 6);
    style->ItemInnerSpacing = ImVec2(8, 6);
    style->IndentSpacing = 25.0f;
    style->ScrollbarSize = 15.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize = 5.0f;
    style->GrabRounding = 3.0f;
    style->FrameBorderSize = 0.f;
    style->ChildBorderSize = 0.f;
    style->WindowBorderSize = 0.f;
    style->WindowTitleAlign = ImVec2(0.5f, 0.5f);

    style->Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    style->Colors[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_TitleBg] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
    style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
    style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
    style->Colors[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
	style->Colors[ImGuiCol_Separator] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
	style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
	style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
    style->Colors[ImGuiCol_Header] = ImVec4(0.74f, 0.14f, 0.14f, 1.00f);
    style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.74f, 0.14f, 0.14f, 0.75f);
    style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.74f, 0.14f, 0.14f, 0.75f);

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(pDevice, pContext);
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    if (true && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

template<typename T>
class cDetour
{
public:
    explicit cDetour<T>(T target, T detour) :m_target(target), m_detour(detour)
    {
        MH_CreateHook(m_target, m_detour, reinterpret_cast<void**>(&m_trampoline));
    }
    ~cDetour()
    {
        MH_DisableHook(m_target);
    }
    T GetTrampoline() const
    {
        return static_cast<T>(m_trampoline);
    }
    bool IsApplied() const
    {
        return m_isEnabled;
    }
    void Apply()
    {
        if (!m_isEnabled)
        {
            m_isEnabled = MH_EnableHook(m_target) == MH_OK;
            if (m_isEnabled)
                memcpy(m_hookBuffer, m_target, sizeof(m_hookBuffer));
        }
    }
    void Remove()
    {
        m_isEnabled = !(m_isEnabled && MH_DisableHook(m_target) == MH_OK);
    }
    void EnsureApply()
    {
        if (memcmp(m_hookBuffer, m_target, sizeof(m_hookBuffer)) != 0)
        {
            DWORD oldProtect;
            VirtualProtect(m_target, sizeof(m_hookBuffer), PAGE_READWRITE, &oldProtect);
            memcpy(m_target, m_hookBuffer, sizeof(m_hookBuffer));
            VirtualProtect(m_target, sizeof(T), oldProtect, &oldProtect);
        }
    }
private:
    T m_trampoline;
    T m_target;
    T m_detour;
    bool m_isEnabled = false;
    char m_hookBuffer[20];

};

class cContext
{
public:
    static cContext& GetInstance();
    template<typename T> cDetour<T>* CreateDetour(T target, T detour)
    {
        auto pDetour = new cDetour<T>(target, detour);
        return pDetour;
    }
    template<typename T> bool ApplyDetour(T target, T detour, cDetour<T>** ppDetour)
    {
        auto pDetour = CreateDetour(target, detour);
        if (pDetour)
        {
            *ppDetour = pDetour;
            pDetour->Apply();
            return true;
        }
        return false;
    }


    void CloseExit()
    {
        if (!(MH_Uninitialize() == MH_OK))
            TerminateProcess(GetCurrentProcess(), -1);
    }
    cContext() {}
    ~cContext() {}

};

bool bInitialized = false;
cContext& cContext::GetInstance()
{
    if (!bInitialized)
        bInitialized = MH_Initialize() == MH_OK;
    static cContext pCtx;
    return pCtx;
}

uintptr_t GetGameAssemblyAddr()
{
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (hSnap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32 modEntry;
        modEntry.dwSize = sizeof(modEntry);
        if (Module32First(hSnap, &modEntry))
        {
            do
            {
                if (!_wcsicmp(modEntry.szModule, L"GameAssembly.dll"))
                {
                    modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnap, &modEntry));
        }
    }
    CloseHandle(hSnap);
    return modBaseAddr;
}

HANDLE hProcess;

template <class T>
static T read(unsigned long long address) {
    T buffer;
    ReadProcessMemory(hProcess, (LPCVOID)address, &buffer, sizeof(T), NULL);
    return buffer;
}

bool init = false;
bool openmenu = true;

int const KeyNames[] = {
    'A',
    'B',
    'C',
    'D',
    'E',
    'F',
    'G',
    'H',
    'I',
    'J',
    'K',
    'L',
    'M',
    'N',
    'O',
    'P',
    'Q',
    'R',
    'S',
    'T',
    'U',
    'V',
    'W',
    'X',
    'Y',
    'Z',
    VK_NUMPAD0,
    VK_NUMPAD1,
    VK_NUMPAD2,
    VK_NUMPAD3,
    VK_NUMPAD4,
    VK_NUMPAD5,
    VK_NUMPAD6,
    VK_NUMPAD7,
    VK_NUMPAD8,
    VK_NUMPAD9,
    VK_MULTIPLY,
    VK_ADD,
    VK_SEPARATOR,
    VK_SUBTRACT,
    VK_DECIMAL,
    VK_DIVIDE,
    VK_F1,
    VK_F2,
    VK_F3,
    VK_F4,
    VK_F5,
    VK_F6,
    VK_F7,
    VK_F8,
    VK_F9,
    VK_F10,
    VK_F11,
    VK_F12,
};

bool Binding = false;
int BindingID = 0;

const char* GetKeyText(int key)
{
    LPARAM lParam;

    UINT sc = MapVirtualKey(key, 0);
    lParam = sc << 16;

    char buf[256];
    GetKeyNameTextA(lParam, buf, 256);
    return buf;
}

void KeyBind(int& key, int width, int id = 0)
{
    bool currentBinding = (Binding && BindingID == id);
    if (ImGui::Button(currentBinding ? "..." : GetKeyText(key), ImVec2(width, 30))) {
        Binding = !Binding;
        BindingID = id;
    }
    if (GetKeyState(VK_ESCAPE) & 0x8000) {
        Binding = false;
        return;
    }
    if (currentBinding) {
        for (int k : KeyNames)
        {
            if (GetKeyState(k) & 0x8000) {
                Binding = false;
                key = k;
            }
        }
    }
}

bool NoShadows = false;
bool NoGrass = false;

int BIND_Upgrade = 'U';
bool BIND_Upgrade_Enabled = true;
int BIND_Remove = 'P';
bool BIND_Remove_Enabled = true;
int BIND_TPA = 'H';
bool BIND_TPA_Enabled = true;
int BIND_KIT = 'K';
bool BIND_KIT_Enabled = true;
int BIND_T = 'T';
bool BIND_T_Enabled = true;

void SaveConfig();
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags, HMODULE hMod)
{
    if (!init)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)))
        {
            pDevice->GetImmediateContext(&pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            window = sd.OutputWindow;
            ID3D11Texture2D* pBackBuffer;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
            pBackBuffer->Release();
            oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
            InitImGui();
            init = true;
        }

        else
            return oPresent(pSwapChain, SyncInterval, Flags);
    }

    auto flags = ImGuiConfigFlags_NoMouseCursorChange | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

    if (GetAsyncKeyState(VK_INSERT) & 1) {
        openmenu = !openmenu;
        if (!openmenu) {
            SaveConfig();
        }
    }

    //if (GetAsyncKeyState(VK_END)) {

    //    kiero::shutdown();
    //}

    if (openmenu) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(300, 350));
        ImGui::Begin(u8"HUNT RUST", nullptr, flags);

        if (ImGui::CollapsingHeader(u8"Оптимизация игры")) {
            ImGui::Checkbox(u8"Отключение травы", &NoGrass);
            ImGui::Checkbox(u8"Отключение теней", &NoShadows);
        }

        if (ImGui::CollapsingHeader(u8"Бинды")) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.29f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.22f, 0.29f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.22f, 0.29f, 1.00f));

            if (ImGui::CollapsingHeader(u8"Киты")) {
                ImGui::Columns(2);
                ImGui::Checkbox(u8"Включен##AA", &BIND_KIT_Enabled);
                ImGui::NextColumn();
                if (!BIND_KIT_Enabled) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                KeyBind(BIND_KIT, ImGui::GetContentRegionAvailWidth(), 0);
                if (!BIND_KIT_Enabled) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
                ImGui::EndColumns();
            }

            if (ImGui::CollapsingHeader(u8"Принять ТП")) {
                ImGui::Columns(2);
                ImGui::Checkbox(u8"Включен##BB", &BIND_TPA_Enabled);
                ImGui::NextColumn();
                if (!BIND_TPA_Enabled) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                KeyBind(BIND_TPA, ImGui::GetContentRegionAvailWidth(), 1);
                if (!BIND_TPA_Enabled) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
                ImGui::EndColumns();
            }

            if (ImGui::CollapsingHeader(u8"ТП Меню")) {
                ImGui::Columns(2);
                ImGui::Checkbox(u8"Включен##CC", &BIND_T_Enabled);
                ImGui::NextColumn();
                if (!BIND_T_Enabled) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                KeyBind(BIND_T, ImGui::GetContentRegionAvailWidth(), 2);
                if (!BIND_T_Enabled) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
                ImGui::EndColumns();
            }

            if (ImGui::CollapsingHeader(u8"Улучшение построек")) {
                ImGui::Columns(2);
                ImGui::Checkbox(u8"Включен##DD", &BIND_Upgrade_Enabled);
                ImGui::NextColumn();
                if (!BIND_Upgrade_Enabled) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                KeyBind(BIND_Upgrade, ImGui::GetContentRegionAvailWidth(), 3);
                if (!BIND_Upgrade_Enabled) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
                ImGui::EndColumns();
            }

            if (ImGui::CollapsingHeader(u8"Удаление построек")) {
                ImGui::Columns(2);
                ImGui::Checkbox(u8"Включен##EE", &BIND_Remove_Enabled);
                ImGui::NextColumn();
                if (!BIND_Remove_Enabled) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                KeyBind(BIND_Remove, ImGui::GetContentRegionAvailWidth(), 4);
                if (!BIND_Remove_Enabled) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
                ImGui::EndColumns();
            }

            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
        }

        ImGui::End();
        ImGui::Render();

        pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    return oPresent(pSwapChain, SyncInterval, Flags);
}

typedef float (__stdcall* CalculateLOD_)();
cDetour<CalculateLOD_>* t_CalculateLOD;

typedef void(__stdcall* Update_)(uintptr_t);
cDetour<Update_>* t_Update;

typedef void(__stdcall* SetShadowsQuality_)(uintptr_t, int);
SetShadowsQuality_ SetShadowsQuality;

void OnUpdate(uintptr_t instance)
{
    if (NoShadows) {
        uintptr_t light = read<uintptr_t>(instance + 0x18);
        if (light == NULL) return;
        SetShadowsQuality(light, 0);
    }
    else {
        t_Update->GetTrampoline()(instance);
    }
}

// RVA: 0x20DF480 Offset: 0x20DE680 VA: 0x1820DF480
//public static string Run(ConsoleSystem.Option options, string strCommand, object[] args) { }

// RVA: 0x20E0D00 Offset: 0x20DFF00 VA: 0x1820E0D00
//public static ConsoleSystem.Option get_Client() { }

FARPROC GetCoreExport(const char* func)
{
    HMODULE hMono = GetModuleHandleA("GameAssembly.dll");
    while (hMono == NULL) {
        hMono = GetModuleHandleA("GameAssembly.dll");
        Sleep(100);
    }
    return GetProcAddress(hMono, func);
}


typedef bool(__stdcall* SendToServer_)(uintptr_t);
SendToServer_ SendToServer;

typedef uintptr_t(__stdcall* NewGString_)(const char*);
NewGString_ NewGString;

// RVA: 0x20DFC80 Offset: 0x20DEE80 VA: 0x1820DFC80
//internal static bool SendToServer(string command) { }

void CycleBinds()
{
    while (true) {
        if (BIND_KIT_Enabled && (GetKeyState(BIND_KIT) & 0x8000)) {
            SendToServer(NewGString("chat.say /kit"));
            Sleep(500);
        }
        if (BIND_Upgrade_Enabled && (GetKeyState(BIND_Upgrade) & 0x8000)) {
            SendToServer(NewGString("chat.say /up"));
            Sleep(500);
        }
        if (BIND_Remove_Enabled && (GetKeyState(BIND_Remove) & 0x8000)) {
            SendToServer(NewGString("chat.say /remove"));
            Sleep(500);
        }
        if (BIND_TPA_Enabled && (GetKeyState(BIND_TPA) & 0x8000)) {
            SendToServer(NewGString("chat.say /tpa"));
            Sleep(500);
        }
        if (BIND_T_Enabled && (GetKeyState(BIND_T) & 0x8000)) {
            SendToServer(NewGString("chat.say /t"));
            Sleep(500);
        }
        Sleep(10);
    }
}

float OnCalculateLOD()
{
    if(NoGrass) return 0.f;
    return t_CalculateLOD->GetTrampoline()();
}

std::string ReadFile(const std::string& path) {
    std::ifstream input_file(path);
    if (!input_file.is_open()) {
        return std::string("");
    }
    return std::string((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
}

void WriteFile(std::string path, std::string content)
{
    std::ofstream output_file(path);
    if (output_file.is_open()) {
        output_file << content;
    }
    output_file.close();
}

/*
bool NoShadows = false;
bool NoGrass = false;

int BIND_Upgrade = 'U';
bool BIND_Upgrade_Enabled = true;
int BIND_Remove = 'P';
bool BIND_Remove_Enabled = true;
int BIND_TPA = 'H';
bool BIND_TPA_Enabled = true;
int BIND_KIT = 'K';
bool BIND_KIT_Enabled = true;
int BIND_T = 'T';
bool BIND_T_Enabled = true;*/

inline bool FileExists(const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

void LoadConfig()
{
    if (!FileExists("HUNTBOOSTER.cfg")) {
        SaveConfig();
        return;
    }
    std::string data = ReadFile("HUNTBOOSTER.cfg");
    json j = json::parse(data);
    NoShadows = j["NoShadows"].get<bool>();
    NoGrass = j["NoGrass"].get<bool>();
    BIND_Upgrade = j["BIND_Upgrade"].get<int>();
    BIND_Remove = j["BIND_Remove"].get<int>();
    BIND_TPA = j["BIND_TPA"].get<int>();
    BIND_T = j["BIND_T"].get<int>();
    BIND_KIT = j["BIND_KIT"].get<int>();
    BIND_Upgrade_Enabled = j["BIND_Upgrade_Enabled"].get<bool>();
    BIND_Remove_Enabled = j["BIND_Remove_Enabled"].get<bool>();
    BIND_TPA_Enabled = j["BIND_TPA_Enabled"].get<bool>();
    BIND_T_Enabled = j["BIND_T_Enabled"].get<bool>();
    BIND_KIT_Enabled = j["BIND_KIT_Enabled"].get<bool>();
}

void SaveConfig()
{
    json j;
    j["NoShadows"] = NoShadows;
    j["NoGrass"] = NoGrass;
    j["BIND_Upgrade"] = BIND_Upgrade;
    j["BIND_Remove"] = BIND_Remove;
    j["BIND_TPA"] = BIND_TPA;
    j["BIND_T"] = BIND_T;
    j["BIND_KIT"] = BIND_KIT;
    j["BIND_Upgrade_Enabled"] = BIND_Upgrade_Enabled;
    j["BIND_Remove_Enabled"] = BIND_Remove_Enabled;
    j["BIND_TPA_Enabled"] = BIND_TPA_Enabled;
    j["BIND_T_Enabled"] = BIND_T_Enabled;
    j["BIND_KIT_Enabled"] = BIND_KIT_Enabled;
    WriteFile("HUNTBOOSTER.cfg", j.dump());
}

void Entrypoint()
{
    LoadConfig();

    hProcess = GetCurrentProcess();

    NewGString = (NewGString_)GetCoreExport("il2cpp_string_new_wrapper");

    uintptr_t ga = GetGameAssemblyAddr();

    auto &ctx = cContext::GetInstance();

    SetShadowsQuality = reinterpret_cast<SetShadowsQuality_>(ga + 0x178AFE0);
    SendToServer = reinterpret_cast<SendToServer_>(ga + 0x20DFC80);
    ctx.ApplyDetour<CalculateLOD_>(reinterpret_cast<CalculateLOD_>(ga + 0x6AA470), reinterpret_cast<CalculateLOD_>(OnCalculateLOD), &t_CalculateLOD);
    ctx.ApplyDetour<Update_>(reinterpret_cast<Update_>(ga + 0x7FDEC0), reinterpret_cast<Update_>(OnUpdate), &t_Update);

    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)CycleBinds, 0, 0, 0);
    
    bool init_hook = false;
    do
    {
        if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
        {
            kiero::bind(8, (void**)&oPresent, hkPresent);
            init_hook = true;
        }
    } while (!init_hook);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if(ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Entrypoint, hModule, 0, 0);
    }
    return TRUE;
}
