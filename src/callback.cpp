#include "main.h"
#include "callback.h"
#include "hotkeys.h"

#include <vector>


struct ModInfo {
        const char* mod_name;
        CallbackFunction callback[CALLBACKTYPE_COUNT];
};


static std::vector<ModInfo> RegisteredMods{};


//TODO: these need to match up perfectly,
//      is there a better way to do this?
static std::vector<RegistrationHandle> DrawHandles{};
static std::vector<RegistrationHandle> ConfigHandles{};
static std::vector<RegistrationHandle> HotkeyHandles{};
static std::vector<RegistrationHandle> AboutHandles{};

static constexpr std::vector<RegistrationHandle>* const CallbackHandles[] = {
        &DrawHandles,
        &ConfigHandles,
        &HotkeyHandles,
        &AboutHandles
};


extern const RegistrationHandle* CallbackGetHandles(CallbackType type, uint32_t* out_count) {
        ASSERT(type < CALLBACKTYPE_COUNT);

        uint32_t count = (uint32_t)CallbackHandles[type]->size();
        if(out_count) {
                *out_count = count;
        }
        return &CallbackHandles[type]->operator[](0);
}


extern const char* CallbackGetName(RegistrationHandle handle) {
        ASSERT(handle < RegisteredMods.size());
        return RegisteredMods[handle].mod_name;
}


extern CallbackFunction CallbackGetCallback(CallbackType type, RegistrationHandle handle) {
        ASSERT(handle < RegisteredMods.size());
        ASSERT(type < CALLBACKTYPE_COUNT);
        CallbackFunction ret{};
        memcpy(&ret, &RegisteredMods[handle].callback[type], sizeof(ret));
        return ret;
}


static RegistrationHandle RegisterMod(const char* mod_name) {
        ASSERT(mod_name != NULL);
        ASSERT(*mod_name && "mod_name cannot be empty");
        ASSERT(strlen(mod_name) < 32 && "mod_name too long >= 32 characters");
        ASSERT(strlen(mod_name) >= 3 && "mod_name too short < 3 characters");
        //todo check for whitespace and other invalid characters inthe mod name

        const auto ret = (uint32_t) RegisteredMods.size();
        RegisteredMods.push_back({ mod_name });
        return ret;
}


static void RegisterCallback(RegistrationHandle owner, CallbackType type, CallbackFunction callback) {
        ASSERT(owner < RegisteredMods.size());
        ASSERT(type < CALLBACKTYPE_COUNT);
        FUNC_PTR callback_function;
        memcpy(&callback_function, &callback, sizeof(callback_function));
        ASSERT(callback_function != NULL);

        auto& info = RegisteredMods[owner];
        FUNC_PTR ptr;
        memcpy(&ptr, &info.callback[type], sizeof(ptr));
        if(ptr == NULL) {
                // only register once, but allow for callbacks to be changed
                CallbackHandles[type]->push_back(owner);
        }
        info.callback[type] = callback;
}

template<typename T>
static inline CallbackFunction to_callback(const T& in) noexcept {
        CallbackFunction ret{ nullptr };
        memcpy(&ret, &in, sizeof(ret));
        return ret;
}


static void RegisterDrawCallback(RegistrationHandle owner, DRAW_CALLBACK callback) {
        RegisterCallback(owner, CALLBACKTYPE_DRAW, to_callback(callback));
}


static void RegisterConfigCallback(RegistrationHandle owner, CONFIG_CALLBACK callback) {
        RegisterCallback(owner, CALLBACKTYPE_CONFIG, to_callback(callback));
}


static void RegisterHotkeyCallback(RegistrationHandle owner, HOTKEY_CALLBACK callback) {
        RegisterCallback(owner, CALLBACKTYPE_HOTKEY, to_callback(callback));
}

static void RegisterAboutCallback(RegistrationHandle owner, DRAW_CALLBACK callback) {
        RegisterCallback(owner, CALLBACKTYPE_ABOUT, to_callback(callback));
}


// This function is best fit next to the hotkey callback registration, but all of the data
// is in the hotkeys.cpp file, so we forward the call to that function instead of trying to
// share data and state between the two
static void RequestHotkey(RegistrationHandle owner, const char* name, uintptr_t userdata) {
        ASSERT(owner < RegisteredMods.size());
        ASSERT(name != NULL);
        ASSERT(*name && "name cannot be empty");
        ASSERT(strlen(name) < 32 && "name too long >= 32 characters");
        //todo check for whitespace and other invalid characters in the hotkey name

        HotkeyRequestNewHotkey(owner, name, userdata, 0);
}


static constexpr struct callback_api_t CallbackAPI {
        RegisterMod,
        RegisterDrawCallback,
        RegisterConfigCallback,
        RegisterHotkeyCallback,
        RequestHotkey,
        RegisterAboutCallback
};


extern constexpr const struct callback_api_t* GetCallbackAPI() {
        return &CallbackAPI;
}
