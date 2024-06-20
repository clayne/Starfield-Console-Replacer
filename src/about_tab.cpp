#include "main.h"
#include "about_tab.h"
#include "callback.h"

static const auto SimpleDraw = GetBetterAPI()->SimpleDraw;

extern void draw_about_tab() {
        uint32_t num_about;
        const auto handles = CallbackGetHandles(CALLBACKTYPE_ABOUT, &num_about);

        static const auto to_string = [](const void* handles, uint32_t index, char* fmt, uint32_t fmt_size) noexcept -> const char* {
                const RegistrationHandle* hs = (const RegistrationHandle*)handles;
                return CallbackGetName(hs[index]);
                };

        static uint32_t selected = UINT32_MAX;
        SimpleDraw->HBoxLeft(0, 12.f);
        SimpleDraw->SelectionList(&selected, handles, num_about, to_string);
        SimpleDraw->HBoxRight();
        if (selected < num_about) {
                const auto cb = CallbackGetCallback(CALLBACKTYPE_ABOUT, handles[selected]);
                ASSERT(cb.about_callback != NULL);
                cb.about_callback(ImGui::GetCurrentContext());
        }
        SimpleDraw->HBoxEnd();
}

extern void AboutTabCallback(void*) {
        char max_path[_MAX_PATH];
        SimpleDraw->Text("BetterConsole Version %s", BETTERCONSOLE_VERSION);
        SimpleDraw->Text("Build ID: %s - %s", __DATE__, __TIME__);
        ImGui::SeparatorText("Help and Support");
        SimpleDraw->LinkButton("BetterConsole on Nexusmods", "https://www.nexusmods.com/starfield/mods/3683");
        SimpleDraw->LinkButton("Constellation by V2 (Discord)", "https://discord.gg/v2-s-collections-1076179431195955290");
        SimpleDraw->LinkButton("Linuxversion on Reddit", "https://www.reddit.com/user/linuxversion/");
        SimpleDraw->LinkButton("Open Log File", GetPathInDllDir(max_path, "BetterConsoleLog.txt"));
        SimpleDraw->LinkButton("Open Config File", GetPathInDllDir(max_path, "BetterConsoleConfig.txt"));
}