#include <tracer_lib/core.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/*
 *
 * Base context functions
 *
 */

static CRITICAL_SECTION gLinkedListCritSect;
static TracerBaseContext* gLinkedListHead = NULL;

TracerContext* tracerCoreCreateContext(int type, int size) {
    assert(size >= sizeof(TracerBaseContext));

    TracerBaseContext* ctx = (TracerBaseContext*)calloc(1, (size_t)size);
    if (!ctx) {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
        return NULL;
    }

    ctx->mSizeOfStruct = size;
    ctx->mTypeFlags = type;
    ctx->mCleanup = tracerCoreCleanupContext;

    EnterCriticalSection(&gLinkedListCritSect);

    if (gLinkedListHead) {
        ctx->mNextLink = gLinkedListHead;
        gLinkedListHead->mPrevLink = ctx;
    }
    gLinkedListHead = ctx;

    LeaveCriticalSection(&gLinkedListCritSect);

    return (TracerContext*)ctx;
}

void tracerCoreCleanupContext(TracerContext* ctx) {
    if (!ctx) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return;
    }

    EnterCriticalSection(&gLinkedListCritSect);

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    if (base->mPrevLink) {
        base->mPrevLink->mNextLink = base->mNextLink;
    }
    if (base->mNextLink) {
        base->mNextLink->mPrevLink = base->mPrevLink;
    }

    if (base == gLinkedListHead) {
        gLinkedListHead = base->mNextLink;
    }

    LeaveCriticalSection(&gLinkedListCritSect);
}

void tracerCoreDestroyContext(TracerContext* ctx) {
    TracerBaseContext* base = (TracerBaseContext*)ctx;
    if (base) {
        base->mCleanup(ctx);
        free((void*)ctx);
    }
}

TracerBool tracerCoreValidateContext(TracerContext* ctx, int type) {
    TracerBaseContext* base = (TracerBaseContext*)ctx;
    if (!base || (base->mTypeFlags & type) != type) {
#ifdef NDEBUG
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
#else
        abort();
#endif
    }
    return eTracerTrue;
}

int tracerCoreGetContextTypeFlags(TracerContext* ctx) {
    TracerBaseContext* base = (TracerBaseContext*)ctx;
    if (base) {
        return base->mTypeFlags;
    }
    return 0;
}

TracerBool tracerCoreEnumContexts(const TracerContextCallback cb,
    void* parameter, int type, TracerBool returnAfterFail) {

    EnterCriticalSection(&gLinkedListCritSect);

    TracerBool result = eTracerTrue;
    TracerBaseContext* current = gLinkedListHead;

    while (current) {
        if ((current->mTypeFlags & type) == type &&
            !cb((TracerContext*)current, parameter)) {

            result = eTracerFalse;

            if (returnAfterFail) {
                goto cleanup;
            }
        }
        current = current->mNextLink;
    }

cleanup:
    LeaveCriticalSection(&gLinkedListCritSect);
    return result;
}

/*
 *
 * Core utility functions (getter, setter...)
 *
 */

static DWORD gTracerLastErrorTlsIndex;
static DWORD gTracerProcessContextTlsIndex;
static DWORD gTracerActiveHwBreakpointTlsIndex;

static TracerHandle gTracerModuleHandle;

void tracerCoreSetLastError(TracerError error) {
    TlsSetValue(gTracerLastErrorTlsIndex, (LPVOID)(int)error);
}

TracerError tracerCoreGetLastError() {
    return (TracerError)(int)TlsGetValue(gTracerLastErrorTlsIndex);
}

void tracerCoreSetProcessContext(TracerContext* ctx) {
    if (!ctx || tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        TlsSetValue(gTracerProcessContextTlsIndex, ctx);
    }
}

TracerContext* tracerCoreGetProcessContext() {
    return (TracerContext*)TlsGetValue(gTracerProcessContextTlsIndex);
}

TracerHandle tracerCoreGetModuleHandle() {
    return gTracerModuleHandle;
}

int tracerCoreGetActiveHwBreakpointIndex() {
    return ((int)TlsGetValue(gTracerActiveHwBreakpointTlsIndex)) - 1;
}

void tracerCoreSetActiveHwBreakpointIndex(int index) {
    TlsSetValue(gTracerActiveHwBreakpointTlsIndex, (LPVOID)(index + 1));
}

typedef struct TracerEnumWindowsParams {
    int             mProcessId;
    TracerHandle    mWindowHandle;
} TracerEnumWindowsParams;

static BOOL CALLBACK tracerCoreFindWindowCallback(HWND hwnd, LPARAM lparam) {
    TracerEnumWindowsParams* params = (TracerEnumWindowsParams*)lparam;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    if (processId == (DWORD)params->mProcessId) {
        params->mWindowHandle = (TracerHandle)hwnd;
        return FALSE;
    }
    return TRUE;
}

TracerHandle tracerCoreFindWindow(int processId) {
    if (processId <= 0) {
        processId = (int)GetCurrentProcessId();
    }
    TracerEnumWindowsParams params = {
        /* mProcessId    = */ processId,
        /* mWindowHandle = */ NULL
    };
    EnumWindows(tracerCoreFindWindowCallback, (LPARAM)&params);
    return params.mWindowHandle;
}

TracerBool tracerCoreSetPrivilege(TracerHandle process, const tchar* privilege, TracerBool enable) {
    LUID luid;
    if (!LookupPrivilegeValue(NULL, privilege, &luid)) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
        return eTracerFalse;
    }

    HANDLE tokenHandle = NULL;
    if (!OpenProcessToken((HANDLE)process, TOKEN_ADJUST_PRIVILEGES, &tokenHandle)) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
        return eTracerFalse;
    }

    TOKEN_PRIVILEGES tokenPrivileges;
    ZeroMemory(&tokenPrivileges, sizeof(tokenPrivileges));

    tokenPrivileges.PrivilegeCount = 1;
    tokenPrivileges.Privileges[0].Luid = luid;
    tokenPrivileges.Privileges[0].Attributes = (enable ? SE_PRIVILEGE_ENABLED : 0);

    TracerBool result = eTracerFalse;

    if (AdjustTokenPrivileges(tokenHandle, FALSE, &tokenPrivileges, 0, NULL, NULL)) {
        result = (GetLastError() == ERROR_SUCCESS);
    }

    if (!result) {
        tracerCoreSetLastError(eTracerErrorInsufficientPermission);
    }

    CloseHandle(tokenHandle);
    return result;
}

/*
 *
 * Hash table based process context management functions
 *
 */

#define CONTEXT_TABLE_SIZE 4091

typedef struct TracerContextBlock {
    int                     mProcessId;  //< Key
    TracerContext*          mContext;    //< Value
} TracerContextBlock;

static CRITICAL_SECTION gProcessContextCritSect;
static TracerContextBlock gProcessContexts[CONTEXT_TABLE_SIZE];

static TracerContextBlock* tracerCoreGetContextBlock(int pid, TracerBool firstFree) {
    TracerContextBlock* current;
    int startBlock = pid % CONTEXT_TABLE_SIZE;
    int currentBlock = startBlock;

    do {
        current = &gProcessContexts[currentBlock];
        if (current->mProcessId == pid) {
            return current;
        }

        if (firstFree && !current->mContext) {
            return current;
        }

        currentBlock = (currentBlock + 1) % CONTEXT_TABLE_SIZE;
    } while (current->mProcessId && currentBlock != startBlock);

    return NULL;
}

TracerContext* tracerCoreGetContextForPID(int pid) {
    TracerContextBlock* block = tracerCoreGetContextBlock(pid, eTracerFalse);
    return block ? block->mContext : NULL;
}

void tracerCoreSetContextForPID(int pid, TracerContext* ctx) {
    // Try to find a matching block
    TracerContextBlock* block = tracerCoreGetContextBlock(pid, eTracerFalse);

    // No match, try to find an unused block
    if (!block) {
        block = tracerCoreGetContextBlock(pid, eTracerTrue);
    }

    if (block) {
        block->mProcessId = pid;
        block->mContext = ctx;
    }
}

TracerBool tracerCoreEnumProcessContexts(const TracerContextCallback cb,
    void* parameter, TracerBool returnAfterFail) {

    TracerBool result = eTracerTrue;

    for (int i = 0; i < CONTEXT_TABLE_SIZE; ++i) {
        TracerContextBlock* block = &gProcessContexts[i];
        if (block->mContext && !cb(block->mContext, parameter)) {
            result = eTracerFalse;

            if (returnAfterFail) {
                goto cleanup;
            }
        }
    }

cleanup:
    return result;
}

void tracerCoreAcquireProcessContextLock() {
    EnterCriticalSection(&gProcessContextCritSect);
}

void tracerCoreReleaseProcessContextLock() {
    LeaveCriticalSection(&gProcessContextCritSect);
}

/*
 *
 * DllMain entry point
 *
 */

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&gLinkedListCritSect);
        InitializeCriticalSection(&gProcessContextCritSect);

        gTracerModuleHandle = instance;
        gTracerLastErrorTlsIndex = TlsAlloc();
        gTracerProcessContextTlsIndex = TlsAlloc();
        gTracerActiveHwBreakpointTlsIndex = TlsAlloc();

        if (gTracerLastErrorTlsIndex == TLS_OUT_OF_INDEXES ||
            gTracerProcessContextTlsIndex == TLS_OUT_OF_INDEXES ||
            gTracerActiveHwBreakpointTlsIndex == TLS_OUT_OF_INDEXES) {

            return FALSE;
        }
        break;
    case DLL_PROCESS_DETACH:
        TlsFree(gTracerLastErrorTlsIndex);
        TlsFree(gTracerProcessContextTlsIndex);
        TlsFree(gTracerActiveHwBreakpointTlsIndex);

        DeleteCriticalSection(&gProcessContextCritSect);
        DeleteCriticalSection(&gLinkedListCritSect);
        break;
    default:
        break;
    }
    return TRUE;
}
