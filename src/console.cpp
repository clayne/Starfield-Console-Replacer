#include "main.h"

#include <varargs.h>
#include <vector>
#include <ctype.h>

// Console print interface:
//48 89 5c 24 ?? 48 89 6c 24 ?? 48 89 74 24 ?? 57 b8 30 10 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 49 - new sig with 1 match
/*
  undefined4 uVar1;
  int iVar2;
  char local_1010 [4096];
  undefined8 local_10;

  local_10 = 0x142883992;
  uVar1 = FUN_140839080();
  FUN_140587784(0x40);

  //calling vsnprintf() with 4096 buffer and using the return value to append "\n\0"
  iVar2 = FUN_1412518e8(local_1010,0x1000,param_2,param_4,param_3);
  if (0 < iVar2) {
    local_1010[iVar2] = '\n';           //note the behavior of adding "\n\0"
    local_1010[iVar2 + 1] = '\0';
    FUN_14288379c(param_1,local_1010,param_2);
  }
  FUN_140587784(uVar1);
  return;
*/

// Console run command interface:
//48 8b c4 48 89 50 ?? 4c 89 40 ?? 4c 89 48 ?? 55 53 56 57 41 55 41 56 41 57 48 8d
/*
check for this in ghidra:

_strnicmp((char *)local_838,"ForEachRef[",0xb)
...
memset(local_c38,0,0x400);
pcVar15 = "float fresult\nref refr\nset refr to GetSelectedRef\nset fresult to ";

*/


// starting console command runner:
// 40 53 48 83 EC ?? 8B 02 48 8B D9 83 F8 ?? 75 ?? 48 8B
/* uint64_t ExecuteStartingConsoleCommand(void* param_1,int *param_2);
*  if (*param_2 == 5) then execute starting console command
* 
undefined8 ExecuteStartingConsoleCommand(longlong param_1,int *param_2)

{
  longlong string_len;
  undefined8 uVar1;

  if (*param_2 == 5) {
    string_len = -1;
    do {
      string_len = string_len + 1;
    } while (PTR_SettingValue_145f9e358[string_len] != '\0');
    if (string_len == 0) {
      return 0;
    }
    ExecuteCommand(DAT_14687ad18);
  }
  else if (*param_2 != 6) {
    return 0;
  }
  if (param_1 != 0) {
    uVar1 = FUN_1405bbbf0();
    FUN_1405bb700(uVar1,param_1);
  }
  return 0;
}
*/


#define OUTPUT_FILE_PATH "BetterConsoleOutput.txt"
#define HISTORY_FILE_PATH "BetterConsoleHistory.txt"
#define NUM_HOTKEY_COMMANDS 16


enum class InputMode : uint32_t {
        Command,
        SearchOutput,
        SearchHistory,
};

static void console_run(void* consolemgr, char* cmd);
static void console_print(void* consolemgr, const char* fmt, va_list args);

static uintptr_t starting_console_command(void* param_1, int* param_2);

static unsigned char* GetGamePausedAddress();

static int CALLBACK_inputtext_cmdline(ImGuiInputTextCallbackData* data);
static int CALLBACK_inputtext_switch_mode(ImGuiInputTextCallbackData* data);
static bool strcasestr(const char* haystack, const char* needle);

static void(*OLD_ConsolePrintV)(void*, const char*, va_list) = nullptr;    
static void(*OLD_ConsoleRun)(void*, char*) = nullptr;
static uintptr_t(*OLD_StartingConsoleCommand)(void*, int*) = nullptr;

static bool IgnoreGamePaused = false;
static bool StartingCommandCompleted = false;
static bool UpdateScroll = false;
static bool UpdateFocus = true;
static int HistoryIndex = -1;
static InputMode CommandMode = InputMode::Command;

static const BetterAPI* API = nullptr;
static const hook_api_t* HookAPI = nullptr;
static const log_buffer_api_t* LogBuffer = nullptr;
static const simple_draw_t* SimpleDraw = nullptr;
static const config_api_t* Config = nullptr;

static char IOBuffer[256 * 1024];
static LogBufferHandle OutputHandle;
static LogBufferHandle HistoryHandle;
static std::vector<uint32_t> SearchOutputLines{};
static std::vector<uint32_t> SearchHistoryLines{};

static RegistrationHandle ModHandle = 0;

struct Command {
        char name[32];
        char data[512];
} static HotkeyCommands[NUM_HOTKEY_COMMANDS];


static void forward_to_old_consoleprint(void* consolemgr, const char* fmt, ...) {
        if (!consolemgr) return;
        va_list args;
        va_start(args, fmt);
        OLD_ConsolePrintV(consolemgr, fmt, args);
        va_end(args);
}


static void console_print(void* consolemgr, const char* fmt, va_list args) {
        auto size = vsnprintf(IOBuffer, sizeof(IOBuffer), fmt, args);
        if (size <= 0) return;
        forward_to_old_consoleprint(consolemgr, "%s", IOBuffer); //send it already converted
        LogBuffer->Append(OutputHandle, IOBuffer);
        IOBuffer[0] = 0;
        UpdateScroll = true;
}


static void console_run(void* consolemgr, char* cmd) {
        LogBuffer->Append(HistoryHandle, cmd);
        if (OLD_ConsoleRun) {
                OLD_ConsoleRun(consolemgr, cmd);
        }
        else {
                DEBUG("console hook not ready when running command: %s", cmd);
        }
}


static void run_comand(const char* command) {
        char cmd[512]; // max console command length
        snprintf(cmd, sizeof(cmd), "%s", command);
        console_run(NULL, cmd);
}


static void SetGamePaused(bool paused) {
        if (!StartingCommandCompleted) return;
        const auto ptr = GetGamePausedAddress();
        if (ptr) *ptr = paused;
}

static uintptr_t starting_console_command(void* param_1, int* param_2) {
        //TODO: should we register listeners for this event ?
        if (*param_2 == 5) { // 5 means the game is ready to run a command
                StartingCommandCompleted = true;
        }
        return OLD_StartingConsoleCommand(param_1, param_2);
}

static void draw_console_window(void* imgui_context) {
        (void)imgui_context;


        if (!OLD_StartingConsoleCommand) {
                SimpleDraw->Text("Could not hook starting console command function.");
                if (ImGui::Button("Ignore")) {
                        OLD_StartingConsoleCommand = [](void*, int*) noexcept -> uintptr_t {
                                return 0;
                                };
                        StartingCommandCompleted = true;
                }
                return;
        }

        // Provide a message to the user about the state of the application without triggering an assert
        // going forward, non-fatal asserts should be the default. I already have the ui working in a
        // stable manner, i should use it to provide feedback not crash the whole game
        if (!(OLD_ConsolePrintV && OLD_ConsoleRun)) {
                if (!OLD_ConsolePrintV) {
                        SimpleDraw->Text("Could not hook console print function.");
                }
                if (!OLD_ConsoleRun) {
                        SimpleDraw->Text("Could not hook console execute function.");
                }

                SimpleDraw->Text("Possible incompatible mod loaded or game update incompatible");
                SimpleDraw->Text("BetterConsole version %s is known to be compatible with game version %u.%u.%u", BETTERCONSOLE_VERSION, (GAME_VERSION >> 24) & 0xFF, (GAME_VERSION >> 16) & 0xFF, (GAME_VERSION>>4) & 0xFFF);
                return;
        }


        if (!StartingCommandCompleted) {
                SimpleDraw->Text("Waiting for game to initialize console...");
                return;
        }


        if (!IgnoreGamePaused) {
                if(GetGamePausedAddress()) {
                        IgnoreGamePaused = true;
                }
                else {
                        SimpleDraw->Text("Cannot find game paused flag address");
                        if(ImGui::Button("Ignore")) {
                                IgnoreGamePaused = true;
                        }
                        return;
                }
        }


        unsigned char* GameisPaused = GetGamePausedAddress();
        if (GameisPaused) {
                if(*GameisPaused) {
                        if (ImGui::Button("Unpause Game")) {
                                *GameisPaused = false;
                        }
                }
                else {
                        if (ImGui::Button("Pause Game")) {
                                *GameisPaused = true;
                        }
                }
                ImGui::SameLine();
        }
        

        ImGui::SetNextItemWidth(-(ImGui::GetFontSize() * 11.5f));
        if (UpdateFocus) {
                ImGui::SetKeyboardFocusHere();
                UpdateFocus = false;
        }

        if (CommandMode == InputMode::Command) {
                if (ImGui::InputText("Command Mode", IOBuffer, sizeof(IOBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion, CALLBACK_inputtext_cmdline)) {
                        HistoryIndex = LogBuffer->GetLineCount(HistoryHandle);
                        console_run(NULL, IOBuffer);
                        IOBuffer[0] = 0;
                        UpdateFocus = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                        LogBuffer->Clear(OutputHandle);
                }
                SimpleDraw->ShowLogBuffer(OutputHandle, UpdateScroll);
        }
        else if (CommandMode == InputMode::SearchOutput) {
                if (ImGui::InputText("Search Output", IOBuffer, sizeof(IOBuffer), ImGuiInputTextFlags_CallbackCompletion, CALLBACK_inputtext_switch_mode)) {
                        SearchOutputLines.clear();
                        for (uint32_t i = 0; i < LogBuffer->GetLineCount(OutputHandle); ++i) {
                                if (strcasestr(LogBuffer->GetLine(OutputHandle, i), IOBuffer)) {
                                        SearchOutputLines.push_back(i);
                                }
                        }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                        LogBuffer->Clear(OutputHandle);
                }
                SimpleDraw->ShowFilteredLogBuffer(OutputHandle, SearchOutputLines.data(), (uint32_t)SearchOutputLines.size(), UpdateScroll);
        }
        else if (CommandMode == InputMode::SearchHistory) {
                if (ImGui::InputText("Search History", IOBuffer, sizeof(IOBuffer), ImGuiInputTextFlags_CallbackCompletion, CALLBACK_inputtext_switch_mode)) {
                        SearchHistoryLines.clear();
                        for (uint32_t i = 0; i < LogBuffer->GetLineCount(HistoryHandle); ++i) {
                                if (strcasestr(LogBuffer->GetLine(HistoryHandle, i), IOBuffer)) {
                                        SearchHistoryLines.push_back(i);
                                }
                        }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                        LogBuffer->Clear(HistoryHandle);
                }
                SimpleDraw->ShowFilteredLogBuffer(HistoryHandle, SearchHistoryLines.data(), (uint32_t)SearchHistoryLines.size(), UpdateScroll);
        }
        else {
                ASSERT(false && "Invalid command mode");
        }

        UpdateScroll = false;
}


static int CALLBACK_inputtext_cmdline(ImGuiInputTextCallbackData* data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
                const auto HistoryMax = (int)LogBuffer->GetLineCount(HistoryHandle);

                if (data->EventKey == ImGuiKey_UpArrow) {
                        --HistoryIndex;
                }
                else if (data->EventKey == ImGuiKey_DownArrow) {
                        ++HistoryIndex;
                }

                if (HistoryIndex < 0) {
                        HistoryIndex = 0;
                }

                if (HistoryIndex >= HistoryMax) {
                        HistoryIndex = HistoryMax - 1;
                }

                if (HistoryMax) {
                        data->DeleteChars(0, data->BufTextLen);
                        data->InsertChars(0, LogBuffer->GetLine(HistoryHandle, HistoryIndex));
                }
        }
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
                return CALLBACK_inputtext_switch_mode(data);
        }

        return 0;
}

static int CALLBACK_inputtext_switch_mode(ImGuiInputTextCallbackData* data) {
        (void)data;
        SearchOutputLines.clear();
        SearchHistoryLines.clear();
        UpdateFocus = true;

        switch (CommandMode)
        {
        case InputMode::Command:
                CommandMode = InputMode::SearchOutput;
                break;
        case InputMode::SearchOutput:
                CommandMode = InputMode::SearchHistory;
                break;
        case InputMode::SearchHistory:
                //fallthrough
        default:
                CommandMode = InputMode::Command;
                //when switching back to comand mode, scroll output to bottom
                UpdateScroll = true;
                break;
        }
        return 0;
}


static bool strcasestr(const char* s, const char* find) {
        char c, sc;
        size_t len;
        if ((c = *find++) != 0) {
                c = (char)tolower((unsigned char)c);
                len = strlen(find);
                do {
                        do {
                                if ((sc = *s++) == 0)
                                        return false;
                        } while ((char)tolower((unsigned char)sc) != c);
                } while (_strnicmp(s, find, len) != 0);
        }
        return true;
}



static void CALLBACK_console_settings(enum ConfigAction action) {
        char keyname[32];


        for (uint32_t i = 0; i < 16; ++i) {
                auto& hc = HotkeyCommands[i];
                snprintf(keyname, sizeof(keyname), "Hotkey Command%u", i);
                Config->ConfigString(action, keyname, hc.data, sizeof(hc.data));
        }
}

static void CALLBACK_console_hotkey(uintptr_t userdata) {
        auto index = (uint32_t)userdata;
        char* hotkey = HotkeyCommands[index].data;
        if (hotkey[0]) {
                API->Console->RunCommand(hotkey);
        }
}


// this should be the only interface between the console replacer and the mod menu code
extern void setup_console(const BetterAPI* api) {
        API = api;

        const auto CB = API->Callback;
        ModHandle = CB->RegisterMod("BetterConsole");
        CB->RegisterDrawCallback(ModHandle, draw_console_window);
        CB->RegisterConfigCallback(ModHandle, CALLBACK_console_settings);
        CB->RegisterHotkeyCallback(ModHandle, CALLBACK_console_hotkey);

        for (uint32_t i = 0; i < NUM_HOTKEY_COMMANDS; ++i) {
                char key_name[32];
                snprintf(key_name, sizeof(key_name), "HotkeyCommand %u", i);
                CB->RequestHotkey(ModHandle, key_name, i);
        }

        LogBuffer = api->LogBuffer;
        HookAPI = API->Hook;
        SimpleDraw = API->SimpleDraw;
        Config = API->Config;

        OutputHandle = LogBuffer->Create("Console Output", OUTPUT_FILE_PATH);
        HistoryHandle = LogBuffer->Restore("Command History", HISTORY_FILE_PATH);

        DEBUG("Hooking print function using AOB method");
        auto hook_print_aob = HookAPI->AOBScanEXE("48 89 5c 24 ?? 48 89 6c 24 ?? 48 89 74 24 ?? 57 b8 30 10 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 49");
        
        if (hook_print_aob) {
                OLD_ConsolePrintV = (decltype(OLD_ConsolePrintV))HookAPI->HookFunction(
                        (FUNC_PTR)hook_print_aob,
                        (FUNC_PTR)console_print
                );
        }

        DEBUG("Hooking run function using AOB method");
        auto hook_run_aob = HookAPI->AOBScanEXE("48 8b c4 48 89 50 ?? 4c 89 40 ?? 4c 89 48 ?? 55 53 56 57 41 55 41 56 41 57 48 8d");
        
        if (hook_run_aob) {
                OLD_ConsoleRun = (decltype(OLD_ConsoleRun))HookAPI->HookFunction(
                        (FUNC_PTR)hook_run_aob,
                        (FUNC_PTR)console_run
                );
        }

        DEBUG("Hooking starting console command using AOB method");
        auto hook_start_aob = HookAPI->AOBScanEXE("40 53 48 83 EC ?? 8B 02 48 8B D9 83 F8 ?? 75 ?? 48 8B");
        
        if (hook_start_aob) {
                OLD_StartingConsoleCommand = (decltype(OLD_StartingConsoleCommand))HookAPI->HookFunction(
                        (FUNC_PTR)hook_start_aob,
                        (FUNC_PTR)starting_console_command
                );
        }

        IOBuffer[0] = 0;
}


static unsigned char* GetGamePausedAddress() {
        static unsigned char* fun = (unsigned char*)GetHookAPI()->AOBScanEXE(
                "48 8b 0d ?? ?? ?? ??" // RCX,QWORD PTR [rip+0x????????]
                " 80 79 ?? 00"         // CMP BYTE PTR [rcx+0x??],0x00 
                " 0f 94 c0"            // SETZ AL (AL = ZF)
                " 88 41 ??"            // MOV BYTE PTR [RCX + 0x??],AL
                " b0 01"               // MOV AL,0x1
                " C3"                  // RET
        );
        if (!fun) return nullptr;
        //DEBUG("Found game paused function: %llX", fun);

        uint32_t offset_gamestate;
        unsigned char offset_pause_flag;
        memcpy(&offset_gamestate, fun + 3, sizeof(offset_gamestate));
        memcpy(&offset_pause_flag, fun + 9, sizeof(offset_pause_flag));
        //DEBUG("Displacement: %X", offset_gamestate);
        //DEBUG("Displacement2: %X", offset_pause_flag);

        // +7 because RIP points to the next instruction
        unsigned char** gamestate = (unsigned char**)(fun + offset_gamestate + 7);
        //DEBUG("Gamestate address: %llX", *gamestate);
        if (!gamestate) return nullptr;

        auto ret = *gamestate + offset_pause_flag;
        //DEBUG("Gamestate: %llX", ret);

        return ret;
}




static constexpr struct console_api_t Console {
        run_comand,
        SetGamePaused
};

extern constexpr const struct console_api_t* GetConsoleAPI() {
        return &Console;
}