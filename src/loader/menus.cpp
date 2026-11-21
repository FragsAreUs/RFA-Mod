// In-game menus for mods (see rfa_menu.h): the loader hosts Dear ImGui over the game's picture.
//
// Where it draws: the game makes its swap chain with a DXGI factory (it imports CreateDXGIFactory),
// so the loader wraps that import and, on the factory it returns, its CreateSwapChain (slot 10 of
// the factory's methods; a COM object's methods are a table of pointers shared by every object of
// its kind, so it is swapped once, as an import is, keeping the old pointer to call on). The game
// is then given a swap chain of the loader's own (ChainProxy) that passes every call on to the real
// one, drawing the menus on the game's finished picture just before Present shows it and letting go
// of the picture before ResizeBuffers resizes it. The real swap chain's own methods are NOT
// swapped: the Steam overlay swaps Present and ResizeBuffers there too, and swaps them again when
// the game changes its display mode, after which its hook and ours called each other until the
// stack ran out (the game ended, without a word, on every video settings change).
//
// When: while the developer console is open (0x28FF1CA), when the game has already stopped the
// player's controls and stopped holding the mouse in the middle of the screen. Their own key
// ([Loader] MenuKey=, F7 unless set; read from the window's messages) opens them alone: it opens the
// console for the game (0xC7DCA0), which our console then neither draws nor gives keys to
// (RFALoader_MenusWant(RFA_MENUS_ALONE)); the key again closes it (0xC7DC80), as ` does. The game hides
// Windows' cursor, so ImGui draws its own. The game reads its own keys and mouse with DirectInput,
// so the window's messages, which ImGui reads, are passed on to the game untouched; the console
// asks RFALoader_MenusWant whether the menus have the mouse or are taking text, and leaves them.
//
// The bar along the bottom of the screen holds each mod's menus (RFAMod_Menu with RFA_MENU_BAR),
// and the mods then draw their windows (RFA_MENU_WINDOWS). Window places are kept in menus.ini.
#define RFA_MOD_NAME "menus"
#include "rfa_mod.h"
#include "rfa_menu.h"
#include "menus.h"
#include "profiler.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include <d3d11.h>
#include <dxgi.h>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM w, LPARAM l);

namespace {

auto& g_consoleOpen = *reinterpret_cast<const volatile BYTE*>(0x28FF1CA);

// The methods wrapped, by their place in the object's table of methods.
constexpr int kCreateSwapChainSlot = 10;  // IDXGIFactory::CreateSwapChain

using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using MenuFn = void(__cdecl*)(const RFAMenu*);

CreateFactoryFn g_createFactory;
CreateSwapChainFn g_createSwapChain;

const std::vector<HMODULE>* g_mods;
std::vector<MenuFn> g_menus;  // each mod's RFAMod_Menu, found on the first frame drawn
bool g_menusFound = false;

IDXGISwapChain* g_chain = nullptr;  // the game's
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
HWND g_window = nullptr;
WNDPROC g_windowProc = nullptr;
ID3D11RenderTargetView* g_target = nullptr;  // the picture, to draw on
UINT g_pictureWidth = 0, g_pictureHeight = 0;

bool g_started = false;  // ImGui is set up
bool g_failed = false;   // and couldn't be
bool g_shown = false;    // the menus were drawn this frame
bool g_alone = false;    // opened with their own key, the console out of sight
UINT g_menuKey = VK_F7;  // that key (0: none)
std::string g_iniPath;

auto& g_consoleOpenByte = *reinterpret_cast<const volatile BYTE*>(0x28FF1CA);
auto const OpenConsole = reinterpret_cast<void(__cdecl*)()>(0xC7DCA0);
auto const CloseConsole = reinterpret_cast<void(__cdecl*)(bool)>(0xC7DC80);

// A key's name as the setting gives it: F1 to F12, Insert, Home, End, PageUp, PageDown, Delete,
// Pause, or one letter or digit. 0 if none.
UINT KeyNamed(const std::wstring& name) {
    if (name.size() >= 2 && (name[0] == L'F' || name[0] == L'f')) {
        const int n = _wtoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    static const struct { const wchar_t* name; UINT key; } named[] = {
        {L"Insert", VK_INSERT}, {L"Home", VK_HOME}, {L"End", VK_END}, {L"PageUp", VK_PRIOR},
        {L"PageDown", VK_NEXT}, {L"Delete", VK_DELETE}, {L"Pause", VK_PAUSE}};
    for (const auto& one : named)
        if (_wcsicmp(name.c_str(), one.name) == 0) return one.key;
    if (name.size() == 1 && iswalnum(name[0])) return towupper(name[0]);
    return 0;
}

// The menus' own key, pressed (on the game's thread, in its window's messages).
void ToggleMenus() {
    if (!g_consoleOpenByte) {
        OpenConsole();
        g_alone = true;
    } else {
        CloseConsole(false);
        g_alone = false;
    }
}

// Swaps slot `slot` of `object`'s methods for `replacement`, keeping the old one in `original`.
template <class Fn>
bool WrapMethod(void* object, int slot, Fn replacement, Fn* original) {
    void** methods = *static_cast<void***>(object);
    if (methods[slot] == reinterpret_cast<void*>(replacement)) return true;
    void* old = methods[slot];
    if (!rfa::WritePointer(&methods[slot], reinterpret_cast<void*>(replacement))) return false;
    *original = reinterpret_cast<Fn>(old);
    return true;
}

// Typing into the main menu's text fields (Level editor > Create level's names: loader.cpp's rfa_text_*). While it
// is on, the window's keyboard messages are ours: its characters go into the text, Enter ends it (kept), Esc ends
// it (put back), and nothing reaches the game (its menu would take the letters for moves and Space for a press).
// The window's messages and the menu's Lua both run on the game's thread; the lock is for safety only.
SRWLOCK g_typingLock = SRWLOCK_INIT;
bool g_typing = false;
bool g_typingFile = false;  // a file name: lower case letters, digits and _ only (capitals made lower, spaces and - made _)
size_t g_typingMost = 0;
std::string g_typed;
int g_typingEnd = -1;  // -1 none, 0 typing, 1 kept (Enter), 2 put back (Esc)

bool Typed(UINT message, WPARAM w, LPARAM l) {
    AcquireSRWLockExclusive(&g_typingLock);
    const bool on = g_typing;
    if (on) {
        if (message == WM_CHAR) {
            wchar_t c = static_cast<wchar_t>(w);
            if (c == L'\b') {
                if (!g_typed.empty()) g_typed.pop_back();
            } else if (c >= 32 && c < 127 && g_typed.size() < g_typingMost) {
                if (g_typingFile) {
                    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
                    if (c == L' ' || c == L'-') c = L'_';
                    const bool fits = (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9' && !g_typed.empty()) || (c == L'_' && !g_typed.empty());
                    if (fits) g_typed.push_back(static_cast<char>(c));
                } else if (c != L'"' && c != L'\\') {  // (the editor's command line carries it)
                    g_typed.push_back(static_cast<char>(c));
                }
            }
        } else if (message == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE) && !(l & (1 << 30))) {  // (not a held key repeating)
            g_typing = false;
            g_typingEnd = w == VK_RETURN ? 1 : 2;
        }
    }
    ReleaseSRWLockExclusive(&g_typingLock);
    return on && (message == WM_CHAR || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_DEADCHAR);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (Typed(message, w, l)) return 0;
    if (message == WM_KEYDOWN && g_menuKey && w == g_menuKey && !(l & (1 << 30)) && !g_failed) {
        ToggleMenus();
        return 0;
    }
    if (g_shown && ImGui::GetCurrentContext()) ImGui_ImplWin32_WndProcHandler(window, message, w, l);
    return CallWindowProcW(g_windowProc, window, message, w, l);
}

std::string Utf8(const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size - 1 : 0, '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &out[0], size, nullptr, nullptr);
    return out;
}

std::string g_menuKeyName;  // the menus' key, as the setting names it

// The menus' look: dark charcoal panels with rust-orange accents (Red Faction's), roomy spacing and
// slightly rounded corners.
void ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(9, 5);
    style.ItemSpacing = ImVec2(9, 6);
    style.ItemInnerSpacing = ImVec2(7, 5);
    style.ScrollbarSize = 14;
    style.GrabMinSize = 12;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.PopupBorderSize = 1;
    style.WindowRounding = 5;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 3;
    style.TabRounding = 3;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    const ImVec4 text(0.93f, 0.90f, 0.85f, 1.00f), dim(0.55f, 0.52f, 0.48f, 1.00f);
    const ImVec4 panel(0.075f, 0.072f, 0.070f, 0.95f), panelDeep(0.050f, 0.048f, 0.047f, 0.97f);
    const ImVec4 frame(0.16f, 0.15f, 0.14f, 1.00f), frameLit(0.24f, 0.20f, 0.17f, 1.00f);
    const ImVec4 rust(0.62f, 0.24f, 0.09f, 1.00f), rustLit(0.80f, 0.34f, 0.12f, 1.00f), rustDeep(0.40f, 0.15f, 0.06f, 1.00f);
    const ImVec4 border(0.30f, 0.20f, 0.14f, 0.60f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = dim;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = panelDeep;
    c[ImGuiCol_MenuBarBg] = panelDeep;
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameLit;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.22f, 0.17f, 1.00f);
    c[ImGuiCol_TitleBg] = panelDeep;
    c[ImGuiCol_TitleBgActive] = rustDeep;
    c[ImGuiCol_TitleBgCollapsed] = panelDeep;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.25f);
    c[ImGuiCol_ScrollbarGrab] = frameLit;
    c[ImGuiCol_ScrollbarGrabHovered] = rustDeep;
    c[ImGuiCol_ScrollbarGrabActive] = rust;
    c[ImGuiCol_CheckMark] = rustLit;
    c[ImGuiCol_SliderGrab] = rust;
    c[ImGuiCol_SliderGrabActive] = rustLit;
    c[ImGuiCol_Button] = rustDeep;
    c[ImGuiCol_ButtonHovered] = rust;
    c[ImGuiCol_ButtonActive] = rustLit;
    c[ImGuiCol_Header] = ImVec4(0.40f, 0.15f, 0.06f, 0.75f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.62f, 0.24f, 0.09f, 0.80f);
    c[ImGuiCol_HeaderActive] = rust;
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = rust;
    c[ImGuiCol_SeparatorActive] = rustLit;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.62f, 0.24f, 0.09f, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = rust;
    c[ImGuiCol_ResizeGripActive] = rustLit;
    c[ImGuiCol_Tab] = frame;
    c[ImGuiCol_TabHovered] = rust;
    c[ImGuiCol_TableHeaderBg] = frame;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.22f, 0.18f, 0.15f, 0.50f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_PlotHistogram] = rust;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.62f, 0.24f, 0.09f, 0.45f);
    c[ImGuiCol_NavCursor] = rustLit;
}

bool MakeTarget() {
    ID3D11Texture2D* picture = nullptr;
    if (FAILED(g_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&picture))) || !picture) return false;
    D3D11_TEXTURE2D_DESC desc;
    picture->GetDesc(&desc);
    g_pictureWidth = desc.Width;
    g_pictureHeight = desc.Height;
    const HRESULT made = g_device->CreateRenderTargetView(picture, nullptr, &g_target);
    picture->Release();
    return SUCCEEDED(made);
}

void LetGoOfTarget() {
    if (g_target) g_target->Release();
    g_target = nullptr;
}

bool Start() {
    if (!MakeTarget()) {
        rfa::Log("Could not reach the game's picture; no menus");
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    g_iniPath = Utf8(rfa::RootDir() + L"\\menus.ini");
    io.IniFilename = g_iniPath.c_str();
    io.LogFilename = nullptr;
    io.MouseDrawCursor = true;
    io.ConfigErrorRecoveryEnableAssert = false;  // a mod's mistake in its menus is put right, never a stop

    // Sized for the picture: as ImGui's own at 1080 lines, larger above.
    const float scale = g_pictureHeight > 1080 ? g_pictureHeight / 1080.0f : 1.0f;
    ApplyTheme();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontSizeBase = 19.0f;
    style.FontScaleMain = scale;
    wchar_t windows[MAX_PATH];
    const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring font = length && length < MAX_PATH ? std::wstring(windows) + L"\\Fonts\\segoeui.ttf" : L"";
    if (font.empty() || GetFileAttributesW(font.c_str()) == INVALID_FILE_ATTRIBUTES ||
        !io.Fonts->AddFontFromFileTTF(Utf8(font).c_str(), 18.0f))
        io.Fonts->AddFontDefault();

    if (!ImGui_ImplWin32_Init(g_window) || !ImGui_ImplDX11_Init(g_device, g_context)) {
        rfa::Log("Could not set up Dear ImGui on the game's window and device; no menus");
        return false;
    }
    if (!g_windowProc) {
        rfa::Log("Could not read the game window's messages; no menus");
        return false;
    }
    rfa::LogDetail("Dear ImGui %s set up on the game's picture (%ux%u)", IMGUI_VERSION, g_pictureWidth, g_pictureHeight);
    return true;
}

void FindMenus() {
    g_menusFound = true;
    if (!g_mods) return;
    for (HMODULE mod : *g_mods)
        if (auto menu = reinterpret_cast<MenuFn>(GetProcAddress(mod, "RFAMod_Menu"))) g_menus.push_back(menu);
    rfa::LogDetail("%u mod(s) with menus", static_cast<unsigned>(g_menus.size()));
}

// Calls a mod's menu; a crash in it ends up in the log, and that mod's menus are off from then on.
bool CallMenu(MenuFn menu, const RFAMenu* info) {
    __try {
        menu(info);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Calls each mod's menus. For their windows, what one leaves unfinished (a window not ended, ...) is
// put right after it, so the next starts clean; inside the bar, which is itself unfinished there,
// that is done once around the whole bar instead.
void CallMenus(int stage) {
    RFAMenu info = {};
    info.size = sizeof(info);
    info.imguiVersion = IMGUI_VERSION;
    info.context = ImGui::GetCurrentContext();
    ImGui::GetAllocatorFunctions(&info.alloc, &info.free, &info.allocData);
    info.stage = stage;
    for (MenuFn& menu : g_menus) {
        if (!menu) continue;
        ImGuiErrorRecoveryState state;
        if (stage == RFA_MENU_WINDOWS) ImGui::ErrorRecoveryStoreState(&state);
        if (!CallMenu(menu, &info)) {
            rfa::Log("A mod's menu crashed; its menus are off");
            menu = nullptr;
        }
        ImGui::SetCurrentContext(info.context);
        if (stage == RFA_MENU_WINDOWS) ImGui::ErrorRecoveryTryToRecoverState(&state);
    }
}

// The bar along the bottom of the screen, with each mod's menus.
void DrawBar() {
    ImGuiErrorRecoveryState state;
    ImGui::ErrorRecoveryStoreState(&state);
    const ImGuiViewport* view = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(view->Pos.x, view->Pos.y + view->Size.y - height));
    ImGui::SetNextWindowSize(ImVec2(view->Size.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##RFA-Mod menus", nullptr, flags) && ImGui::BeginMenuBar()) {
        if (g_menuKey) ImGui::TextDisabled("RFA-Mod  (%s)", g_menuKeyName.c_str());
        else ImGui::TextDisabled("RFA-Mod");
        CallMenus(RFA_MENU_BAR);
        ImGui::EndMenuBar();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::ErrorRecoveryTryToRecoverState(&state);
}

void DrawMenus() {
    g_shown = false;
    if (!g_consoleOpen) g_alone = false;
    if (!g_consoleOpen || g_failed) return;
    if (!g_started) {
        g_started = true;
        if (!Start()) {
            g_failed = true;
            return;
        }
    }
    if (!g_target && !MakeTarget()) return;
    if (!g_menusFound) FindMenus();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)  // the picture may be drawn at another size than the window's
        io.DisplayFramebufferScale = ImVec2(g_pictureWidth / io.DisplaySize.x, g_pictureHeight / io.DisplaySize.y);
    ImGui::NewFrame();
    DrawBar();
    CallMenus(RFA_MENU_WINDOWS);
    ImGui::Render();

    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* depth = nullptr;
    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth);
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depth);
    for (ID3D11RenderTargetView* target : targets)
        if (target) target->Release();
    if (depth) depth->Release();
    g_shown = true;
}

// The swap chain the game is given: the real one behind it does the work (see the top).
class ChainProxy final : public IDXGISwapChain {
public:
    explicit ChainProxy(IDXGISwapChain* real) : real_(real) {}

    // IUnknown. The game's references are the real swap chain's: when its last one goes, so does this.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(IDXGIObject) || id == __uuidof(IDXGIDeviceSubObject) ||
            id == __uuidof(IDXGISwapChain)) {
            *out = static_cast<IDXGISwapChain*>(this);
            AddRef();
            return S_OK;
        }
        return real_->QueryInterface(id, out);  // a newer kind of swap chain: the real one, unwrapped
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return real_->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override {
        IDXGISwapChain* real = real_;
        real->AddRef();
        const bool last = real->Release() == 1;  // this Release is its last
        if (last && g_chain == real) {
            LetGoOfTarget();  // our view of its picture goes before it does
            g_chain = nullptr;
        }
        const ULONG left = real->Release();
        if (last) delete this;
        return left;
    }

    // IDXGIObject, IDXGIDeviceSubObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT size, const void* data) override { return real_->SetPrivateData(name, size, data); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* unknown) override { return real_->SetPrivateDataInterface(name, unknown); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* size, void* data) override { return real_->GetPrivateData(name, size, data); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id, void** parent) override { return real_->GetParent(id, parent); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id, void** device) override { return real_->GetDevice(id, device); }

    // IDXGISwapChain
    HRESULT STDMETHODCALLTYPE Present(UINT sync, UINT flags) override {
        if (real_ == g_chain && !(flags & DXGI_PRESENT_TEST)) {
            DrawMenus();
            ProfilerFramePresented();  // counts the frame ([Loader] Profile=1)
        }
        return real_->Present(sync, flags);
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT buffer, REFIID id, void** surface) override { return real_->GetBuffer(buffer, id, surface); }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput* target) override { return real_->SetFullscreenState(fullscreen, target); }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen, IDXGIOutput** target) override { return real_->GetFullscreenState(fullscreen, target); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override { return real_->GetDesc(desc); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) override {
        if (real_ == g_chain) LetGoOfTarget();
        return real_->ResizeBuffers(count, width, height, format, flags);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* mode) override { return real_->ResizeTarget(mode); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override { return real_->GetContainingOutput(output); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* stats) override { return real_->GetFrameStatistics(stats); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override { return real_->GetLastPresentCount(count); }

private:
    IDXGISwapChain* real_;
};

HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** chain) {
    const HRESULT made = g_createSwapChain(factory, device, desc, chain);
    if (FAILED(made) || !chain || !*chain || !desc || !device) return made;
    if (g_window) {  // the game made a new one (a display change): draw on that from now on
        if (desc->OutputWindow != g_window) return made;
        LetGoOfTarget();
        g_chain = *chain;
        *chain = new ChainProxy(g_chain);
        rfa::LogDetail("The game made a new swap chain: %ux%u", desc->BufferDesc.Width, desc->BufferDesc.Height);
        return made;
    }
    ID3D11Device* d3d = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void**>(&d3d))) || !d3d) return made;
    g_device = d3d;
    g_device->GetImmediateContext(&g_context);
    g_chain = *chain;
    *chain = new ChainProxy(g_chain);
    g_window = desc->OutputWindow;
    g_windowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
    rfa::LogDetail("The game's swap chain: %ux%u; menus will show while the console is open", desc->BufferDesc.Width, desc->BufferDesc.Height);
    return made;
}

HRESULT WINAPI CreateFactory(REFIID id, void** factory) {
    const HRESULT made = g_createFactory(id, factory);
    if (SUCCEEDED(made) && factory && *factory && !g_createSwapChain &&
        !WrapMethod(*factory, kCreateSwapChainSlot, &CreateSwapChain, &g_createSwapChain))
        rfa::Log("Could not wrap the DXGI factory's CreateSwapChain; no menus");
    return made;
}

}  // namespace

bool BeginTyping(const std::string& text, bool fileName, size_t most) {
    if (!g_window || !g_windowProc) return false;  // the game's window isn't ours to read yet
    AcquireSRWLockExclusive(&g_typingLock);
    g_typing = true;
    g_typingFile = fileName;
    g_typingMost = most;
    g_typed = text.substr(0, most);
    g_typingEnd = 0;
    ReleaseSRWLockExclusive(&g_typingLock);
    return true;
}

int TypingState(std::string* text) {
    AcquireSRWLockShared(&g_typingLock);
    if (text) *text = g_typed;
    const int end = g_typingEnd;
    ReleaseSRWLockShared(&g_typingLock);
    return end;
}

void EndTyping() {
    AcquireSRWLockExclusive(&g_typingLock);
    g_typing = false;
    g_typingEnd = -1;
    ReleaseSRWLockExclusive(&g_typingLock);
}

void InstallMenus(const std::vector<HMODULE>* mods) {
    g_mods = mods;
    const std::wstring key = rfa::IniString(L"Loader", L"MenuKey", L"F7");
    g_menuKey = KeyNamed(key);
    g_menuKeyName = Utf8(key);
    rfa::LogDetail(g_menuKey ? "Menus: %ls opens them" : "Menus: no key of their own (\"%ls\"); open the console", key.c_str());
    if (!rfa::HookImport(GetModuleHandleW(nullptr), "dxgi.dll", "CreateDXGIFactory", &CreateFactory, &g_createFactory))
        rfa::Log("Could not hook the game's CreateDXGIFactory; no menus");
}

extern "C" __declspec(dllexport) int __cdecl RFALoader_MenusWant(int what) {
    if (what == RFA_MENUS_ALONE) return g_alone && g_consoleOpenByte ? 1 : 0;  // from the first frame on
    if (!g_shown) return 0;
    const ImGuiIO& io = ImGui::GetIO();
    switch (what) {
        case RFA_MENUS_SHOWN: return 1;
        case RFA_MENUS_MOUSE: return io.WantCaptureMouse ? 1 : 0;
        case RFA_MENUS_KEYBOARD: return io.WantTextInput ? 1 : 0;
        case RFA_MENUS_ALONE: return g_alone ? 1 : 0;
        default: return 0;
    }
}
