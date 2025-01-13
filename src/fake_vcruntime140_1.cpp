#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

 extern "C" EXCEPTION_DISPOSITION __declspec(dllexport) __CxxFrameHandler4(void* A, void* B, void* C, void* D) {
         static auto hmodule = LoadLibraryExW(L"vcruntime140_1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
         static decltype(&__CxxFrameHandler4) real_func = nullptr;
         if (!hmodule) goto ERROR_OUT;
         real_func = (decltype(&__CxxFrameHandler4))GetProcAddress(hmodule, "__CxxFrameHandler4");
         if (!real_func) goto ERROR_OUT;
         return real_func(A, B, C, D);

 ERROR_OUT:
         MessageBoxA(NULL, __func__, "Betterconsole Crashed!", 0);
         std::abort();
}
