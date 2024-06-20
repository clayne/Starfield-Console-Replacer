#include "main.h"
#include "gui.h"
#include "hotkeys.h"
#include "about_tab.h"




extern void draw_gui() {
        // Allow focus of first registered tab (usually betterconsole)
        ImGuiTabItemFlags default_tab = ImGuiTabItemFlags_None;

        static bool once = false;
        if (!once) {
                const auto screen = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2{ screen.x / 4, screen.y / 4 });
                ImGui::SetNextWindowSize(ImVec2{ screen.x / 2 , screen.y / 2 });
                default_tab = ImGuiTabItemFlags_SetSelected;
                once = true;
        }

        auto imgui_context = ImGui::GetCurrentContext();
        ImGui::Begin("BetterConsole");
        ImGui::BeginTabBar("mod tabs");

        if (GetSettings()->FontScaleOverride == 0) {
                GetSettingsMutable()->FontScaleOverride = 100;
        }
        ImGui::SetWindowFontScale((float) GetSettings()->FontScaleOverride / 100.f);

        //TODO: the internal modmenu tab could be part of the internal plugin
        //      and thus called by the tab callback iteration routine like any other plugin
        if (ImGui::BeginTabItem("Mod Menu")) {
                ImGui::BeginTabBar("mod menu tabs");
                if (ImGui::BeginTabItem("Settings")) {
                        // imlemented in settings.cpp - i don't want to export the internals of the settings imlpementation
                        // just to use them in one other place, instead i'd rather add a ui function to the settings code
                        draw_settings_tab();
                        ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Hotkeys")) {
                        // implemented in hotkeys.cpp
                        draw_hotkeys_tab();
                        ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("About")) {
                        // Implemented in about_tab.cpp
                        draw_about_tab();
                        ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
                ImGui::EndTabItem();
        }
        

        uint32_t draw_count = 0;
        const auto draw_callback = CallbackGetHandles(CALLBACKTYPE_DRAW, &draw_count);
        for (uint32_t i = 0; i < draw_count; ++i) {
                const auto handle = draw_callback[i];
                ImGui::PushID(handle);
                if (ImGui::BeginTabItem(CallbackGetName(handle), nullptr, default_tab)) {
                        default_tab = ImGuiTabItemFlags_None;
                        CallbackGetCallback(CALLBACKTYPE_DRAW, handle).draw_callback(imgui_context);
                        ImGui::EndTabItem();
                }
                ImGui::PopID();
        }
        ImGui::EndTabBar();
        ImGui::End();
}
