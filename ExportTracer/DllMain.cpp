#include <windows.h>
#include <tlhelp32.h>
#include <codecvt>

#include "Log.h"

#include "ExportTracer.h"
#include "Hook.h"

using namespace std;

static bool install_loadlibrary_hook();
static uintptr_t __fastcall loadlibrary_hook(hook_arg_t arg, func_args *args);
static uintptr_t library_function_hook(export_hookinfo *info);
static void suspend_threads();
static void resume_threads();

// Don't clean these up because laziness
Hook *loadlib_hook = NULL;
vector<export_tracer *> tracer_list;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    HANDLE hThread = NULL;
    if (fdwReason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    if (!install_loadlibrary_hook()) {
        // error
    }
    return TRUE;
}

static bool install_loadlibrary_hook()
{
    if (!initialize_log()) {
        error("Failed to initialized log");
        return false;
    }
    loadlib_hook = new Hook();
    if (!loadlib_hook) {
        error("Failed to initialize LoadLibraryW hook");
        return false;
    }
    loadlib_hook->init((uintptr_t)LoadLibraryW, loadlibrary_hook, (hook_arg_t)loadlib_hook);
    // create a 'post-hook' instead of a 'pre-hook'
    if (!loadlib_hook->install_hook(false)) {
        error("Failed to hook LoadLibrary");
        return false;
    }
    success("Hooked LoadLibrary");
    return true;
}

static uintptr_t __fastcall loadlibrary_hook(hook_arg_t arg, func_args *args)
{
    Hook *hook = (Hook *)arg;
    const wchar_t *wlib = (const wchar_t *)args->arg1;
    uintptr_t hlib = NULL;

    // convert to ascii for our inferior logging routines
    wstring_convert<codecvt_utf8_utf16<wchar_t>> conv;
    string lib = conv.to_bytes(wlib);

    // callback to original to create the 'post-hook'
    hook->unhook();
    hlib = (uintptr_t)LoadLibraryW(wlib);
    hook->rehook();

    info("Loadlibrary(%s) called", lib.c_str());

    // create new tracer for user32
    export_tracer *tracer = new export_tracer();
    if (!tracer->init(lib)) {
        error("Failed to init tracer");
        resume_threads();
        return hlib;
    }

    // pause everything so we can hook this library
    suspend_threads();

    info("Hooking exports of %s...", lib.c_str());

    // hook all exports from user32 and redirect to user32_hook
    if (!tracer->hook_all_exports(library_function_hook)) {
        error("Failed to hook all user32 exports");
        resume_threads();
        return hlib;
    }

    success("Hooked all exports of %s", lib.c_str());

    resume_threads();

    tracer_list.push_back(tracer);

    return hlib;
}

static uintptr_t library_function_hook(export_hookinfo *info)
{
    const char *fmt;
    switch (info->hook->get_num_args()) {
    case 0:
    default:
        fmt = "%s(%u):%s()";
        break;
    case 1:
        fmt = "%s(%u):%s(%p)";
        break;
    case 2:
        fmt = "%s(%u):%s(%p, %p)";
        break;
    case 3:
        fmt = "%s(%u):%s(%p, %p, %p)";
        break;
    case 4:
        fmt = "%s(%u):%s(%p, %p, %p, %p)";
        break;
    }
    info(fmt, info->module.c_str(), info->ord, info->name.c_str(),
        info->args->arg1, info->args->arg2,
        info->args->arg3, info->args->arg4);
    return 0;
}

static void suspend_threads()
{
    info("Suspending threads...");
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (h == INVALID_HANDLE_VALUE) {
        error("Failed to create toolhelp snapshot to suspend threads");
        return;
    }
    THREADENTRY32 te;
    DWORD cur_pid = GetCurrentProcessId();
    DWORD cur_tid = GetCurrentThreadId();
    te.dwSize = sizeof(te);
    if (!Thread32First(h, &te)) {
        error("Failed to iterate to first thread");
        return;
    }
    do {
        // Suspend all threads EXCEPT the one we want to keep running
        if (te.th32ThreadID == cur_tid || te.th32OwnerProcessID != cur_pid) {
            continue;
        }
        HANDLE thread = ::OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (!thread) {
            continue;
        }
        SuspendThread(thread);
        CloseHandle(thread);
    } while (Thread32Next(h, &te));
    CloseHandle(h);
    success("Suspended all threads");
}

static void resume_threads()
{
    info("Resuming threads...");
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (h == INVALID_HANDLE_VALUE) {
        error("Failed to create toolhelp snapshot to suspend threads");
        return;
    }
    THREADENTRY32 te;
    DWORD cur_pid = GetCurrentProcessId();
    DWORD cur_tid = GetCurrentThreadId();
    te.dwSize = sizeof(te);
    if (!Thread32First(h, &te)) {
        error("Failed to iterate to first thread");
        return;
    }
    do {
        // Suspend all threads EXCEPT the one we want to keep running
        if (te.th32ThreadID == cur_tid || te.th32OwnerProcessID != cur_pid) {
            continue;
        }
        HANDLE thread = ::OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (!thread) {
            continue;
        }
        ResumeThread(thread);
        CloseHandle(thread);
    } while (Thread32Next(h, &te));
    CloseHandle(h);
    success("Resumed all threads");
}


