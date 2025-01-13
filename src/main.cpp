#include "main.h"

#include "../imgui/imgui_impl_win32.h"

#include <dxgi1_6.h>
#include <d3d12.h>
#include <intrin.h>

#include "gui.h"
#include "console.h"
#include "broadcast_api.h"
#include "std_api.h"
#include "hotkeys.h"
#include "std_api.h"
#include "game_hooks.h"
#include "winapi.h"
#include "csv_parser.h"
#include "parser.h"

#include "d3d11on12ui.h"

#define BETTERAPI_IMPLEMENTATION
#include "../betterapi.h"


BC_DLLEXPORT SFSEPluginVersionData SFSEPlugin_Version = {
        1, // SFSE api version
        1, // Plugin version
        "BetterConsole",
        "Linuxversion",
        1, // AddressIndependence::Signatures
        1, // StructureIndependence::NoStructs
        {GAME_VERSION, 0}, 
        0, //does not rely on any sfse version
        0, 0 //reserved fields
};

static void OnHotkeyActivate(uintptr_t);
static void SetupModMenu();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HRESULT FAKE_ResizeBuffers(IDXGISwapChain3* This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
static HRESULT FAKE_Present(IDXGISwapChain3* This, UINT SyncInterval, UINT PresentFlags);
static LRESULT FAKE_Wndproc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

//static decltype(FAKE_ResizeBuffers)* OLD_ResizeBuffers = nullptr;
//static decltype(FAKE_Present)* OLD_Present = nullptr;
static decltype(FAKE_Wndproc)* OLD_Wndproc = nullptr;
static FUNC_PTR* SwapChainVTable = nullptr;

static HINSTANCE self_module_handle = nullptr;
static bool should_show_ui = false;
static bool betterapi_load_selftest = false;
static bool setting_pause_on_ui_open = false;

#define EveryNFrames(N) []()->bool{static unsigned count=0;if(++count==(N)){count=0;}return !count;}()


static char DLL_DIR[MAX_PATH]{};

extern const char* GetPathInDllDir(char* path_max_buffer, const char* filename) {
        ASSERT(DLL_DIR[0] != '\0');

        char* p = path_max_buffer;
        
        unsigned i, j;

        for (i = 0; DLL_DIR[i]; ++i) {
                *p++ = DLL_DIR[i];
        }

        for (j = 0; filename[j]; ++j) {
                *p++ = filename[j];
        }

        *p = 0;

        return path_max_buffer;
}




//this is where the api* that all clients use comes from
static const BetterAPI API {
        GetHookAPI(),
        GetLogBufferAPI(),
        GetSimpleDrawAPI(),
        GetCallbackAPI(),
        GetConfigAPI(),
        GetStdAPI(),
        GetGameHookAPI(),
        GetWindowsAPI(),
        GetCSVAPI(),
        GetParserAPI()
};


extern "C" __declspec(dllexport) const BetterAPI * GetBetterAPI() {
        return &API;
}


extern ModMenuSettings* GetSettingsMutable() {
        static ModMenuSettings Settings{};
        return &Settings;
}


extern const ModMenuSettings* GetSettings() {
        return GetSettingsMutable();
}


struct CPPObject {
        FUNC_PTR* vTable;
};


#define MAX_QUEUES 4
struct SwapChainQueue {
        ID3D12CommandQueue* Queue;
        IDXGISwapChain* SwapChain;
        uint64_t Age;
} static Queues[MAX_QUEUES];


static HRESULT(*OLD_CreateSwapChainForHwnd)(
        IDXGIFactory2* This,
        ID3D12Device* Device,
        HWND hWnd,
        const DXGI_SWAP_CHAIN_DESC1* pDesc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
        IDXGIOutput* pRestrictToOutput,
        IDXGISwapChain1** ppSwapChain
        ) = nullptr;

static HRESULT FAKE_CreateSwapChainForHwnd(
        IDXGIFactory2* This,
        ID3D12Device* Device,
        HWND hWnd,
        const DXGI_SWAP_CHAIN_DESC1* pDesc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
        IDXGIOutput* pRestrictToOutput,
        IDXGISwapChain1** ppSwapChain
) {
        ASSERT(ppSwapChain != NULL);
        DEBUG("*ppSwapCHain = %p", *ppSwapChain);

        auto ret = OLD_CreateSwapChainForHwnd(This, Device, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
        DEBUG("Factory: %p, Device: %p, HWND: %p, SwapChain: %p, Owner: %p -> Ret: %X", This, Device, hWnd, *ppSwapChain, ppSwapChain, ret);

        if (ret != S_OK) return ret;

        ASSERT(*ppSwapChain != NULL);

        DEBUG("New Swapchain = %p", *ppSwapChain);

        bool swapchain_inserted = false;
        for (unsigned i = 0; i < MAX_QUEUES; ++i) {
                //the device parameter of this function is actually a commandqueue in dx12
                if (Queues[i].Queue == (ID3D12CommandQueue*)Device) {
                        //if the device is already in the queue, just update the swapchain parameter
                        Queues[i].SwapChain = *ppSwapChain;
                        DEBUG("REPLACE [%u]: CommandQueue: '%p', SwapChain: '%p'", i, Queues[i].Queue, Queues[i].SwapChain);
                        Queues[i].Age = 0;
                        swapchain_inserted = true;
                        break;
                } else if (Queues[i].Queue == nullptr) {
                        //otherwise fill empty queue
                        Queues[i].Queue = (ID3D12CommandQueue*)Device;
                        Queues[i].SwapChain = *ppSwapChain;
                        Queues[i].Age = 0;
                        DEBUG("ADD [%u]: CommandQueue: '%p', SwapChain: '%p'", i, Queues[i].Queue, Queues[i].SwapChain);
                        swapchain_inserted = true;
                        break;
                }
                else {
                        DEBUG("UNUSED [%u]: CommandQueue: '%p', SwapChain: '%p', Age: %llu", i, Queues[i].Queue, Queues[i].SwapChain, Queues[i].Age);
                        Queues[i].Age++;
                }
        }

        //ok, try removing the oldest one
        if (!swapchain_inserted) {
                unsigned highest = 0;

                for (unsigned i = 0; i < MAX_QUEUES; ++i) {
                        if (Queues[i].Age > Queues[highest].Age) {
                                highest = i;
                        }
                }

                Queues[highest].Age = 0;
                Queues[highest].Queue = (ID3D12CommandQueue*)Device;
                Queues[highest].SwapChain = *ppSwapChain;
                DEBUG("Overwrite[%u]: CommandQueue: '%p', SwapChain: '%p'", highest, Queues[highest].Queue, Queues[highest].SwapChain);
        }

        for(unsigned i = 0; i < MAX_QUEUES; ++i) {
                DEBUG("FINAL STATE [%u]: CommandQueue: '%p', SwapChain: '%p', Age: %llu", i, Queues[i].Queue, Queues[i].SwapChain, Queues[i].Age);
        }

        auto proc = (decltype(OLD_Wndproc))GetWindowLongPtrW(hWnd, GWLP_WNDPROC);
        if ((uint64_t)FAKE_Wndproc != (uint64_t)proc) {
                OLD_Wndproc = proc;
                SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)FAKE_Wndproc);
                DEBUG("Input Hook: OLD_Wndproc: %p, Current_Wndproc: %p, NEW_Wndproc: %p", OLD_Wndproc, proc, FAKE_Wndproc);
        }
        else {
                DEBUG("WndProc already hooked");
        }

        enum : unsigned {
                QueryInterface,
                AddRef,
                Release,
                GetPrivateData,
                SetPrivateData,
                SetPrivateDataInterface,
                GetParent,
                GetDevice,
                Present,
                GetBuffer,
                SetFullscreenState,
                GetFullscreenState,
                GetDesc,
                ResizeBuffers,
        };
        
        static bool once = false;
        if (!once) {
                once = true;
                const auto obj = (CPPObject*)*ppSwapChain;
                DEBUG("ppSwapChain = %p, *ppSwapChain = %p, *ppSwapchain->vTable = %p, *ppSwapchain->vTable[Present] = %p", ppSwapChain, obj, obj->vTable, obj->vTable[Present]);
                SwapChainVTable = obj->vTable;
        }

        return ret;
}


static HRESULT(*OLD_CreateDXGIFactory2)(UINT, REFIID, void**) = nullptr;
static HRESULT FAKE_CreateDXGIFactory2(UINT Flags, REFIID RefID, void **ppFactory) {
        auto ret = OLD_CreateDXGIFactory2(Flags, RefID, ppFactory);

        static bool once = 1;
        if (once) {
                once = 0;

                // now that we are out of dllmain, complete the initialization of betterconsole
                SetupModMenu();

                enum {
                        QueryInterface,
                        AddRef,
                        Release,
                        SetPrivateData,
                        SetPrivateDataInterface,
                        GetPrivateData,
                        GetParent,
                        EnumAdapters,
                        MakeWindowAssociation,
                        GetWindowAssociation,
                        CreateSwapChain,
                        CreateSoftwareAdapter,
                        EnumAdapters1,
                        IsCurrent,
                        IsWindowedStereoEnabled,
                        CreateSwapChainForHwnd
                };

                // Using a stronger hook here because otherwise the steam overlay will not function
                // not sure why a vmt hook doesnt work here, steam checks and rejects?
                FUNC_PTR* fp = *(FUNC_PTR**)*ppFactory;
                DEBUG("HookFunction: CreateSwapChainForHwnd");
                OLD_CreateSwapChainForHwnd = (decltype(OLD_CreateSwapChainForHwnd))API.Hook->HookFunction(fp[CreateSwapChainForHwnd], (FUNC_PTR)FAKE_CreateSwapChainForHwnd);
        }

        return ret;
}


static void Callback_Config(ConfigAction action) {
        auto c = GetConfigAPI();
        auto s = GetSettingsMutable();
        c->ConfigU32(action, "FontScaleOverride", &s->FontScaleOverride);
        c->ConfigBool(action, "Pause Game when BetterConsole opened", &setting_pause_on_ui_open);
 
        //do this last until i have this working with the official api
        if (action == ConfigAction_Write) {
                HotkeySaveSettings();
        }

        ImGui::GetIO().FontGlobalScale = s->FontScaleOverride / 100.0f;
}


static UINT(*OLD_GetRawInputData)(HRAWINPUT hri, UINT cmd, LPVOID data, PUINT data_size, UINT hsize) = nullptr;

UINT FAKE_GetRawInputData(HRAWINPUT hri, UINT cmd, LPVOID data, PUINT data_size, UINT hsize) {
        auto ret = OLD_GetRawInputData(hri, cmd, data, data_size, hsize);
        if (data == NULL) return ret;

        if (cmd == RID_INPUT) {
                auto input = (RAWINPUT*)data;

                if (input->header.dwType == RIM_TYPEKEYBOARD) {
                        auto keydata = input->data.keyboard;
                        if (keydata.Message == WM_KEYDOWN || keydata.Message == WM_SYSKEYDOWN) {
                                if (HotkeyReceiveKeypress(keydata.VKey)) {
                                        goto HIDE_INPUT_FROM_GAME;
                                }
                        }
                }

                if (should_show_ui == false) return ret;

        HIDE_INPUT_FROM_GAME:
                //hide input from the game when shouldshowui is true and data is not null
                input->header.dwType = RIM_TYPEHID; //game ignores typehid messages
        }

        return ret;
}


static void SetupModMenu() {
        DEBUG("Initializing BetterConsole...");
        DEBUG("BetterConsole Version: " BETTERCONSOLE_VERSION);
        
        OLD_GetRawInputData = (decltype(OLD_GetRawInputData)) API.Hook->HookFunctionIAT("user32.dll", "GetRawInputData", (FUNC_PTR)FAKE_GetRawInputData);
        DEBUG("Hook GetRawInputData: %p", OLD_GetRawInputData);

        //i would prefer not hooking multiple win32 apis but its more update-proof than engaging with the game's wndproc
        static BOOL(*OLD_ClipCursor)(const RECT*) = nullptr;
        static decltype(OLD_ClipCursor) FAKE_ClipCursor = [](const RECT* rect) -> BOOL {
                // When the imgui window is open only pass through clipcursor(NULL);
                return OLD_ClipCursor((should_show_ui) ? NULL : rect);
        };
        OLD_ClipCursor = (decltype(OLD_ClipCursor)) API.Hook->HookFunctionIAT("user32.dll", "ClipCursor", (FUNC_PTR)FAKE_ClipCursor);
        DEBUG("Hook ClipCursor: %p", OLD_ClipCursor);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.MouseDrawCursor = true;
        ImGui::StyleColorsDark();
        DEBUG("ImGui one time init completed!");

        // Setup all game-specific hooks
        GameHook_Init();

        // Gather all my friends!
        BroadcastBetterAPIMessage(&API);
        ASSERT(betterapi_load_selftest == true);

        // Load any settings from the config file and call any config callbacks
        LoadSettingsRegistry();
}

extern "C" __declspec(dllexport) bool SFSEPlugin_Load(const SFSEInterface*) { return true; }


static int OnBetterConsoleLoad(const struct better_api_t* api) {
        ASSERT(api == &API && "Betterconsole already loaded?? Do you have multiple versions of BetterConsole installed?");
        
        // The console part of better console is now minimally coupled to the mod menu
        setup_console(api);

        //should the hotkeys code be an internal plugin too?
        const auto handle = api->Callback->RegisterMod("(internal)");
        api->Callback->RegisterConfigCallback(handle, Callback_Config);
        api->Callback->RegisterHotkeyCallback(handle, OnHotkeyActivate);
        
        //force hotkey for betterconsole default action using internal api
        HotkeyRequestNewHotkey(handle, "BetterConsole", 0, VK_F1);
        
        betterapi_load_selftest = true;
        DEBUG("Self Test Complete");
        return 0;
}

static HRESULT (*OLD_D3D11On12CreateDevice)(
        IUnknown* pDevice,
        UINT Flags,
        CONST D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        IUnknown* CONST* ppCommandQueues,
        UINT NumQueues,
        UINT NodeMask,
        void** ppDevice,
        void** ppImmediateContext,
        D3D_FEATURE_LEVEL* pChosenFeatureLevel) = nullptr;

static HRESULT WINAPI FAKE_D3D11On12CreateDevice(
        IUnknown* pDevice,
        UINT Flags,
        CONST D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        IUnknown* CONST* ppCommandQueues,
        UINT NumQueues,
        UINT NodeMask,
        void** ppDevice,
        void** ppImmediateContext,
        D3D_FEATURE_LEVEL* pChosenFeatureLevel) {
        DEBUG("Device = %p", pDevice);
        DEBUG("Flags = %X", Flags);
        DEBUG("FeatureLevels = %u", FeatureLevels);
        for (unsigned i = 0; i < FeatureLevels; ++i) {
                DEBUG("[%u] %X", i, pFeatureLevels[i]);
        }
        DEBUG("NumQueues = %u", NumQueues);
        for (unsigned i = 0; i < NumQueues; ++i) {
                DEBUG("[%u] %p", i, ppCommandQueues[i]);
        }
        DEBUG("NodeMask = %u", NodeMask);
        auto ret = OLD_D3D11On12CreateDevice(
                pDevice,
                Flags,
                pFeatureLevels,
                FeatureLevels,
                ppCommandQueues,
                NumQueues,
                NodeMask,
                ppDevice,
                ppImmediateContext,
                pChosenFeatureLevel);
        DEBUG("*ppDevice = %p", *ppDevice);
        DEBUG("*ppImmediateContext = %p", *ppImmediateContext);
        if (pChosenFeatureLevel) {
                DEBUG("*pChosenFeatureLevel = %u", *pChosenFeatureLevel);
        }
        return ret;
}

#include <d3d11on12.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE self, DWORD fdwReason, LPVOID) {
        if (fdwReason == DLL_PROCESS_ATTACH) {
                /* lock the linker/dll loader until hooks are installed, TODO: make sure this code path is fast */
                static bool RunHooksOnlyOnce = true;
                ASSERT(RunHooksOnlyOnce == true); //i want to know if this assert ever gets triggered
                //while (!IsDebuggerPresent()) Sleep(100);

                // get the path of the current DLL and save it in a buffer for use later
                // most file functions (like logging) need to work with a file in the same folder as the betterconsole dl
                // so we need to do this very early - its the first function call of dllmain()
                GetModuleFileNameA(self, DLL_DIR, MAX_PATH);
                char* n = DLL_DIR;
                while (*n) ++n;
                while ((n != DLL_DIR) && (*n != '\\')) --n;
                ++n;
                *n = 0;

                // just hook this one function the game needs to display graphics, then lazy hook the rest when it's called later
                OLD_CreateDXGIFactory2 = (decltype(OLD_CreateDXGIFactory2))API.Hook->HookFunctionIAT("sl.interposer.dll", "CreateDXGIFactory2", (FUNC_PTR)FAKE_CreateDXGIFactory2);
                if (!OLD_CreateDXGIFactory2) {
                        OLD_CreateDXGIFactory2 = (decltype(OLD_CreateDXGIFactory2))API.Hook->HookFunctionIAT("dxgi.dll", "CreateDXGIFactory2", (FUNC_PTR)FAKE_CreateDXGIFactory2);
                }
                ASSERT(OLD_CreateDXGIFactory2 != NULL);

                OLD_D3D11On12CreateDevice = (decltype(OLD_D3D11On12CreateDevice))API.Hook->HookFunction((FUNC_PTR)D3D11On12CreateDevice, (FUNC_PTR)FAKE_D3D11On12CreateDevice);

                RunHooksOnlyOnce = false;
        }
        return TRUE;
}


static HRESULT FAKE_Present(IDXGISwapChain3* This, UINT SyncInterval, UINT PresentFlags) {
        if (EveryNFrames(300)) {
                DEBUG("render heartbeat, showing ui: %s", (should_show_ui)? "true" : "false");
        }

        static IDXGISwapChain3* last_swapchain = nullptr;
        static ID3D12CommandQueue* command_queue = nullptr;

        SwapChainQueue* best_match = nullptr;
        for (uint32_t i = 0; i < MAX_QUEUES; ++i) {
                if (Queues[i].SwapChain == This) {
                        if (best_match == nullptr || Queues[i].Age < best_match->Age) {
                                best_match = &Queues[i];
                        }
                }
        }
        ASSERT(best_match != nullptr);
        
        if (command_queue != best_match->Queue) {
                command_queue = best_match->Queue;
                DX11_ReleaseIfInitialized();
        }


        if (last_swapchain != This) {
                DEBUG("Swapchain Change: OLD = %p, NEW = %p, commandqueue = %p", last_swapchain, This, command_queue);
                last_swapchain = This;
                DX11_ReleaseIfInitialized();
        }
        

        if (should_show_ui) {
                DX11_InitializeOrRender(This, command_queue);
        }
        else {
                //should i need this?
                DX11_ReleaseIfInitialized();
        }

        return DX11_CallPresent(This, SyncInterval, PresentFlags);
}


static LRESULT FAKE_Wndproc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        static HWND last_window_handle = nullptr;

        if (EveryNFrames(300)) {
                DEBUG("input heartbeat");
        }

        if (last_window_handle != hWnd) {
                DEBUG("Window handle changed: %p -> %p", last_window_handle, hWnd);
                last_window_handle = hWnd;

                ImGuiIO& io = ImGui::GetIO();

                if (io.BackendPlatformUserData) {
                        ImGui_ImplWin32_Shutdown();
                }
                ImGui_ImplWin32_Init(hWnd);
        }

        /* for debugging */
        if (!OLD_GetRawInputData && uMsg == WM_KEYDOWN) {
                // allow toggling the ui in the test renderer
                if (wParam == VK_F1) {
                        OnHotkeyActivate(0);
                }

                if (wParam == VK_F2) {
                        const auto Parser = GetParserAPI();
                        float f = 0;
                        Parser->ParseFloat(" \t \r \n \v  +1.67e-4 \n \r \t \v", &f);
                        DEBUG("f: %f", f);
                }
        }

        if (uMsg == WM_SIZE || uMsg == WM_CLOSE) {
                DX11_ReleaseIfInitialized();
        }

        if (should_show_ui) {
                ClipCursor(NULL);
                ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
        }

        return CallWindowProcW(OLD_Wndproc, hWnd, uMsg, wParam, lParam);
}


static void OnHotkeyActivate(uintptr_t) {
        should_show_ui = !should_show_ui;
        DEBUG("ui toggled, Showing ui %s", (should_show_ui)? "True" : "False");

        static bool hooks_installed = false;
        if (!hooks_installed) {
                hooks_installed = true;

                DX11_InstallSwapChainHooks((void**)SwapChainVTable, FAKE_Present);
                DEBUG("DirectX Hooks installed");
        }

        if (setting_pause_on_ui_open) {
                GetGameHookAPI()->SetGamePaused(should_show_ui);
        }

        if (!should_show_ui) {
                //when you close the UI, settings are saved
                SaveSettingsRegistry();
        }
}