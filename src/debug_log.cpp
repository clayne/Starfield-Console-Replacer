#include "main.h"

#include <Windows.h>

#include <stdio.h>

#include <atomic>

static thread_local char format_buffer[1024];
static constexpr auto buffer_size = sizeof(format_buffer);
static uint32_t frame_count{ 0 };


static void write_log(const char* const str) noexcept {
        static HANDLE debugfile{ INVALID_HANDLE_VALUE };
        
        if (debugfile == INVALID_HANDLE_VALUE) {
                static std::atomic_flag lock{ ATOMIC_FLAG_INIT };
                for (unsigned i = 0; std::atomic_flag_test_and_set_explicit(&lock, std::memory_order_acquire); ++i) {
                        Sleep(100);
                        if (i == 10) return; //give up log write after 1 second of locked atomic (we might be trying to recusively log)
                }

                if (debugfile == INVALID_HANDLE_VALUE) {
                        char path[MAX_PATH];
                        debugfile = CreateFileA(GetPathInDllDir(path, "BetterConsoleLog.txt"), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        const auto log_header{ " Thread |  Frame | File   : Func    : Line > Message\n" };
                        WriteFile(debugfile, log_header, strlen(log_header), NULL, NULL);
                }

                std::atomic_flag_clear_explicit(&lock, std::memory_order_release);
        }

        if (debugfile == INVALID_HANDLE_VALUE) {
                MessageBoxA(NULL, "Could not open logfile 'BetterConsoleLog.txt'", "ASSERTION FAILURE", 0);
                abort();
        }

        if (!debugfile) {
                MessageBoxA(NULL, "Invalid handle to 'BetterConsoleLog.txt'", "ASSERTION FAILURE", 0);
                abort();
        }

        if (FALSE == WriteFile(debugfile, str, (DWORD)strnlen(str, 1024), NULL, NULL)) {
                MessageBoxA(NULL, "Could not write to 'BetterConsoleLog.txt'", "ASSERTION FAILURE", 0);
                abort();
        }

        if (FALSE == FlushFileBuffers(debugfile)) {
                MessageBoxA(NULL, "Could not sync 'BetterConsoleLog.txt'", "ASSERTION FAILURE", 0);
                abort();
        }
}


static const char* const filename_only(const char* const file_path) noexcept {
        for (size_t i = 0, s = 0;; ++i) {
                if (file_path[i] == '/' || file_path[i] == '\\') s = i;
                if (file_path[i] == '\0') return (file_path + s + 1);
        }
} 


extern void DebugImpl(const char* const filename, const char* const func, int line, const char* const fmt, ...) noexcept {
        static thread_local auto thread_id{ GetCurrentThreadId() };

        auto bytes = snprintf(format_buffer, buffer_size, "0x%04X|%8u|%s:%s:%d>", thread_id, frame_count, filename_only(filename), func, line);
        ASSERT(bytes > 0);
        ASSERT(bytes < buffer_size);

        va_list args;
        va_start(args, fmt);
        bytes += vsnprintf(&format_buffer[bytes], buffer_size - bytes, fmt, args);
        va_end(args);
        ASSERT(bytes > 0);
        ASSERT(bytes < buffer_size);

        format_buffer[bytes++] = '\n';
        format_buffer[bytes] = '\0';
        ASSERT(bytes < buffer_size);

        write_log(format_buffer);
}


extern void AssertImpl [[noreturn]] (const char* const filename, const char* const func, int line, const char* const text) noexcept {
        snprintf(
                format_buffer,
                buffer_size,
                "At frame    %u\n"
                "In file     '%s'\n"
                "In function '%s'\n"
                "On line     '%d'\n"
                "Message:    '%s'",
                frame_count,
                filename_only(filename),
                func,
                line,
                text
        );
        // make sure to show the messagebox before writing to the log
        // sometimes log writing was the assert...
        MessageBoxA(NULL, format_buffer, "BetterConsole Crashed!", 0);
        write_log("!!! ASSERTION FAILURE !!!\n");
        write_log(format_buffer);
        abort();
}


extern void TraceImpl(const char* const filename, const char* const func, int line, const char* const fmt, ...) noexcept {
        static bool init = false;
        const auto bytes = snprintf(format_buffer, buffer_size, "%8u|%8u|%s:%s:%d> ", GetCurrentThreadId(), frame_count, filename_only(filename), func, line);
        ASSERT(bytes > 0 && "trace buffer too small ?");
        va_list args;
        va_start(args, fmt);
        const auto bytes2 = vsnprintf(format_buffer + bytes, buffer_size - bytes, fmt, args);
        ASSERT(bytes2 > 0 && "trace buffer too small ?");
        format_buffer[bytes + bytes2] = '\r';
        format_buffer[bytes + bytes2 + 1] = '\n';
        format_buffer[bytes + bytes2 + 2] = '\0';
        va_end(args);
        if (!init) {
                AllocConsole();
                FILE* file = nullptr;
                freopen_s(&file, "CONIN$", "rb", stdin);
                freopen_s(&file, "CONOUT$", "wb", stdout);
                freopen_s(&file, "CONOUT$", "wb", stderr);
        }
        fputs(format_buffer, stdout);
}


extern void DebugFrameFinished(void) noexcept {
        frame_count++;
}