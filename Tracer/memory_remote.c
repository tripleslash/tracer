
#include "memory_remote.h"

#include <assert.h>
#include <stdio.h>
#include <tchar.h>
#include <TlHelp32.h>
#include <Shlwapi.h>

#pragma comment(lib, "shlwapi")

static TracerBool tracerMemoryRemoteInit(TracerContext* ctx);

static TracerBool tracerMemoryRemoteShutdown(TracerContext* ctx);

static size_t tracerMemoryRemoteWrite(TracerContext* ctx, void* address, const void* buffer, size_t size);

static size_t tracerMemoryRemoteRead(TracerContext* ctx, const void* address, void* buffer, size_t size);

static void* tracerMemoryRemoteAlloc(TracerContext* ctx, size_t size);

static void tracerMemoryRemoteFree(TracerContext* ctx, void* address);

static TracerHandle tracerMemoryRemoteFindModule(TracerContext* ctx, const tchar* dllName);

static TracerHandle tracerMemoryRemoteCallNamedExport(TracerContext* ctx, const tchar* module, const char* exportName, void* parameter);

static TracerHandle tracerMemoryRemoteCallNamedExportEx(TracerContext* ctx, const tchar* module, const char* exportName, void* parameter, int timeout);

static int tracerMemoryRemoteCallLocalExport(TracerContext* ctx, const char* exportName, const TracerStruct* parameter);


TracerContext* tracerCreateRemoteMemoryContext(int type, int size, int pid) {
    assert(size >= sizeof(TracerRemoteMemoryContext));
    assert(pid >= 0);

    TracerContext* ctx = tracerCreateMemoryContext(type | eTracerMemoryContextRemote, size, pid);
    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupRemoteMemoryContext;

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    memory->mWriteMemory = tracerMemoryRemoteWrite;
    memory->mReadMemory = tracerMemoryRemoteRead;
    memory->mAllocMemory = tracerMemoryRemoteAlloc;
    memory->mFreeMemory = tracerMemoryRemoteFree;
    memory->mFindModule = tracerMemoryRemoteFindModule;

    DWORD accessFlags = PROCESS_VM_OPERATION
        | PROCESS_VM_READ
        | PROCESS_VM_WRITE
        | PROCESS_QUERY_INFORMATION
        | PROCESS_CREATE_THREAD;

    memory->mProcessHandle = (TracerHandle)OpenProcess(accessFlags, FALSE, (DWORD)pid);

    if (!memory->mProcessHandle) {
        tracerCoreDestroyContext(ctx);
        tracerCoreSetLastError(eTracerErrorInsufficientPermission);
        return NULL;
    }

    if (!tracerMemoryRemoteInit(ctx)) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    return ctx;
}

void tracerCleanupRemoteMemoryContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContextRemote)) {
        return;
    }
    tracerMemoryRemoteShutdown(ctx);
    tracerCleanupMemoryContext(ctx);
}

static TracerBool tracerMemoryRemoteInit(TracerContext* ctx) {
    wchar_t fileName[MAX_PATH];
    wchar_t filePath[MAX_PATH];

    memset(fileName, 0, sizeof(fileName));
    memset(filePath, 0, sizeof(filePath));

    // This is the name of the tracer DLL (e.g. trace.dll)
	TracerHandle localModule = tracerCoreGetModuleHandle();
    GetModuleFileNameW((HMODULE)localModule, fileName, MAX_PATH);

    // Get the full path to the DLL (e.g. C:\Tracer\trace.dll)
    if (!PathCanonicalizeW(filePath, fileName)) {
        memcpy(filePath, fileName, sizeof(fileName));
    }

    // Determine the number of bytes in the path string
    size_t bufferLength = (wcslen(filePath) + 1) * sizeof(wchar_t);

    // Allocate space in the remote process to hold the dll file path string
    void* remotePath = tracerMemoryRemoteAlloc(ctx, bufferLength);
    
    if (!remotePath) {
        // Allocation failed, remote process could be out of memory
        return eTracerFalse;
    }

    int result = 0;

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;

    // Write the dll file path into the address space of the remote process
    if (tracerMemoryRemoteWrite(ctx, remotePath, filePath, bufferLength)) {

        // Launch a new thread on the LoadLibrary export function inside the
        // remote process. This will load our dll into the address space of
        // the remote process.

        TracerHandle callResult = tracerMemoryRemoteCallNamedExportEx(
            ctx, TEXT("kernel32.dll"), "LoadLibraryW", remotePath, -1);

        if (callResult) {
            memory->mModuleHandle = callResult;

            TracerInit init = {
                /* mSizeOfStruct             = */ sizeof(TracerInit),
                /* mVersion                  = */ TLIB_VERSION,
                /* mAcquireSeDebugPrivilege  = */ eTracerFalse,
            };

            result = tracerMemoryRemoteCallLocalExport(
                ctx, "tracerInitEx", (TracerStruct*)&init);

            if (result) {
                TracerAttachProcess attach = {
                    /* mSizeOfStruct         = */ sizeof(TracerAttachProcess),
                    /* mProcessId            = */ -1,
                };

                result = tracerMemoryRemoteCallLocalExport(
                    ctx, "tracerAttachProcessEx", (TracerStruct*)&attach);

                // If tracerAttachProcessEx failed, a call to tracerShutdownEx must be made
                if (!result) {
                    TracerShutdown shutdown = {
                        /* mSizeOfStruct     = */ sizeof(TracerShutdown),
                    };

                    tracerMemoryRemoteCallLocalExport(
                        ctx, "tracerShutdownEx", (TracerStruct*)&shutdown);

                    // We don't care about the return value
                }
            }

            // If either tracerInit or tracerAttachProcess failed
            if (!result) {

                // Call the FreeLibrary function to unload the DLL inside the remote process
                tracerMemoryRemoteCallNamedExportEx(ctx,
                    TEXT("kernel32.dll"), "FreeLibrary", memory->mModuleHandle, -1);

                tracerCoreSetLastError(eTracerErrorRemoteInterop);
                memory->mModuleHandle = NULL;
            }
        }
    }

    tracerMemoryRemoteFree(ctx, remotePath);
    return (TracerBool)result;
}

static TracerBool tracerMemoryRemoteShutdown(TracerContext* ctx) {
    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;

    if (!memory->mModuleHandle) {
        return eTracerTrue;
    }

    TracerShutdown shutdown = {
        /* mSizeOfStruct     = */ sizeof(TracerShutdown),
    };

    int result = tracerMemoryRemoteCallLocalExport(
        ctx, "tracerShutdownEx", (TracerStruct*)&shutdown);

    if (result) {
        tracerMemoryRemoteCallNamedExportEx(ctx,
            TEXT("kernel32.dll"), "FreeLibrary", memory->mModuleHandle, -1);

        memory->mModuleHandle = NULL;
    } else {
        tracerCoreSetLastError(eTracerErrorRemoteInterop);
    }

    return (TracerBool)result;
}

static size_t tracerMemoryRemoteWrite(TracerContext* ctx, void* address, const void* buffer, size_t size) {
    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;

    SIZE_T bytesWritten = 0;
    HANDLE process = (HANDLE)memory->mProcessHandle;

    if (!WriteProcessMemory(process, address, buffer, size, &bytesWritten)) {

        // The WinAPI call failed, maybe we should get some more diagnostic info from GetLastError

        tracerCoreSetLastError(eTracerErrorSystemCall);
        return 0;
    }
    return (size_t)bytesWritten;
}

static size_t tracerMemoryRemoteRead(TracerContext* ctx, const void* address, void* buffer, size_t size) {
    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;

    SIZE_T bytesRead = 0;
    HANDLE process = (HANDLE)memory->mProcessHandle;

    if (!ReadProcessMemory(process, address, buffer, size, &bytesRead)) {

        // The WinAPI call failed, maybe we should get some more diagnostic info from GetLastError

        tracerCoreSetLastError(eTracerErrorSystemCall);
        return 0;
    }
    return (size_t)bytesRead;
}

static void* tracerMemoryRemoteAlloc(TracerContext* ctx, size_t size) {
    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    HANDLE process = (HANDLE)memory->mProcessHandle;

    void* buffer = VirtualAllocEx(process, NULL, (SIZE_T)size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

    if (!buffer) {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
    }
    return buffer;
}

static void tracerMemoryRemoteFree(TracerContext* ctx, void* address) {
    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    HANDLE process = (HANDLE)memory->mProcessHandle;

    if (!VirtualFreeEx(process, address, 0, MEM_RELEASE)) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
    }
}

static TracerHandle tracerMemoryRemoteFindModule(TracerContext* ctx, const tchar* dllName) {
    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    
    if (!dllName) {
        // If we pass NULL for the dllName argument, the handle of the tracer module itself
        // should be returned (similar to GetModuleHandle).

        return memory->mModuleHandle;
    }

    TracerHandle result = NULL;

    DWORD processId = (DWORD)memory->mProcessId;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);

    if (snapshot != INVALID_HANDLE_VALUE) {

        MODULEENTRY32 entry;
        entry.dwSize = sizeof(entry);

        if (Module32First(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID != processId) {
                    // The snapshot contained a process ID that we aren't even looking for
                    continue;
                }
                if (!_tcsicmp(entry.szModule, dllName) || !_tcsicmp(entry.szExePath, dllName)) {
                    // Module name matches with the name we're looking for

                    result = (TracerHandle)entry.hModule;

                    if (!result || result == INVALID_HANDLE_VALUE) {
                        result = (TracerHandle)entry.modBaseAddr;
                    }
                    break;
                }
            } while (Module32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
    } else {
        tracerCoreSetLastError(eTracerErrorSystemCall);
    }

    return result;
}

static TracerHandle tracerMemoryRemoteCallNamedExport(TracerContext* ctx,
    const tchar* module, const char* exportName, void* parameter) {

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TracerHandle localHandle = tracerCoreGetModuleHandle();

    if (module != NULL) {
        // If we pass NULL as module name, we want to use the tracer.dll module handle
        localHandle = (TracerHandle)GetModuleHandle(module);
    }

    TracerHandle remoteHandle = tracerMemoryRemoteFindModule(ctx, module);

    if (!localHandle || !remoteHandle) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
        return NULL;
    }

    DWORD_PTR procAddr = (DWORD_PTR)GetProcAddress((HMODULE)localHandle, exportName);

    if (!procAddr) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
        return NULL;
    }

    // Rebase the address of the function to the remote module
    procAddr = (DWORD_PTR)remoteHandle + (procAddr - (DWORD_PTR)localHandle);

    DWORD threadId = 0;

    HANDLE thread = CreateRemoteThread((HANDLE)memory->mProcessHandle, NULL, 0,
        (LPTHREAD_START_ROUTINE)procAddr, parameter, 0, &threadId);

    if (!thread) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
    }

    return (TracerHandle)thread;
}

TracerHandle tracerMemoryRemoteCallNamedExportEx(TracerContext* ctx,
    const tchar* module, const char* exportName, void* parameter, int timeout) {

    TracerHandle thread = tracerMemoryRemoteCallNamedExport(
        ctx, module, exportName, parameter);

    if (!thread) {
        return NULL;
    }

    TracerHandle result = NULL;

    DWORD waitTimeout = (timeout < 0) ? INFINITE : (DWORD)timeout;

    if (WaitForSingleObject((HANDLE)thread, waitTimeout) == WAIT_OBJECT_0) {

        // API returns WAIT_OBJECT_0 if the thread terminated

        DWORD exitCode = 0;
        if (GetExitCodeThread((HANDLE)thread, &exitCode)) {
            result = (TracerHandle)exitCode;
        } else {
            tracerCoreSetLastError(eTracerErrorSystemCall);
        }
    } else {
        // Thread didn't terminate in the specified timeout interval
        tracerCoreSetLastError(eTracerErrorWaitTimeout);
    }

    CloseHandle((HANDLE)thread);
    return result;
}

#define MAX_EXPORT_NAME_LENGTH 256

static int tracerMemoryRemoteCallLocalExport(TracerContext* ctx, const char* exportName, const TracerStruct* parameter) {
    char decoratedExportName[MAX_EXPORT_NAME_LENGTH];
    snprintf(decoratedExportName, MAX_EXPORT_NAME_LENGTH, "_%s@4", exportName);

    void* buffer = tracerMemoryRemoteAlloc(ctx, parameter->mSizeOfStruct);

    if (!buffer) {
        return 0;
    }

    int result = 0;

    if (tracerMemoryRemoteWrite(ctx, buffer, parameter, parameter->mSizeOfStruct)) {
        result = (int)tracerMemoryRemoteCallNamedExportEx(ctx, NULL, decoratedExportName, buffer, -1);
    }

    tracerMemoryRemoteFree(ctx, buffer);
    return result;
}
