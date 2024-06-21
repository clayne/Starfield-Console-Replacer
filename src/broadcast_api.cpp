#include "main.h"

#include <Windows.h>
#include <Psapi.h>

extern void BroadcastBetterAPIMessage(const struct better_api_t *API) {
        char cur_dir[MAX_PATH]; //this will be the exe folder
        GetCurrentDirectoryA(MAX_PATH, cur_dir);

        auto cur_dir_len = (uint32_t)strlen(cur_dir);

        DEBUG("Current folder: '%s'", cur_dir);

        DWORD needed = 0;
        EnumProcessModules(GetCurrentProcess(), NULL, 0, &needed);

        DWORD size = (needed | 127) + 1;
        HMODULE* handles = (HMODULE*)malloc(size);
        ASSERT(handles != NULL);

        EnumProcessModules(GetCurrentProcess(), handles, size, &needed);

        auto count = needed / sizeof(*handles);

        for (unsigned i = 0; i < count; ++i) {
                if (!handles[i]) {
                        continue;
                }
                
                char path[MAX_PATH];
                GetModuleFileNameA(handles[i], path, MAX_PATH);

                //skip all modules that cant be mods (all dll mods would be in the game folder or a subfolder)
                if (_strnicmp(path, cur_dir, cur_dir_len)) {
                        continue;
                }

                const char* short_path = path + cur_dir_len + 1; // +1 to skip '\\'

                DEBUG("Checking module: '%s'", short_path); 

                const auto BetterAPICallback = (void(*)(const struct better_api_t*)) GetProcAddress(handles[i], "BetterConsoleReceiver");
                
                if (BetterAPICallback) {
                        DEBUG("BetterConsoleReceiver found in mod: %s", short_path);
                        BetterAPICallback(API);
                }
        }

        free(handles);
}