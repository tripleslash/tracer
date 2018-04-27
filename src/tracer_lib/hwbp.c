
#include <tracer_lib/hwbp.h>

#include <TlHelp32.h>

typedef struct TracerHwBreakpoint {
    int                         mIndex;
    int                         mThreadId;
    struct TracerHwBreakpoint*  mNextLink;
} TracerHwBreakpoint;

int tracerHwBreakpointGetBits(uintptr_t dw, int lowBit, int bits) {
    uintptr_t mask = (1 << bits) - 1;
    return (dw >> lowBit) & mask;
}

void tracerHwBreakpointSetBits(uintptr_t* dw, int lowBit, int bits, int newValue) {
    uintptr_t mask = (1 << bits) - 1;
    *dw = (*dw & ~(mask << lowBit)) | (newValue << lowBit);
}

int tracerSetHwBreakpointOnContext(void* address, int length, PCONTEXT ctx, TracerHwBpCond cond) {
    if (!address) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return -1;
    }

    switch (length) {
    case 1: length = 0; break;
    case 2: length = 1; break;
    case 4: length = 3; break;
    default:
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return -1;
    }

    // Find first available hardware register index
    int index = -1;

    if (!ctx->Dr0 && !tracerHwBreakpointGetBits(ctx->Dr7, 0 << 1, 1)) {
        index = 0;
    } else if (!ctx->Dr1 && !tracerHwBreakpointGetBits(ctx->Dr7, 1 << 1, 1)) {
        index = 1;
    } else if (!ctx->Dr2 && !tracerHwBreakpointGetBits(ctx->Dr7, 2 << 1, 1)) {
        index = 2;
    } else if (!ctx->Dr3 && !tracerHwBreakpointGetBits(ctx->Dr7, 3 << 1, 1)) {
        index = 3;
    }

    if (index >= 0) {

        switch (index) {
        case 0: ctx->Dr0 = (uintptr_t)address; break;
        case 1: ctx->Dr1 = (uintptr_t)address; break;
        case 2: ctx->Dr2 = (uintptr_t)address; break;
        case 3: ctx->Dr3 = (uintptr_t)address; break;
        default: assert(FALSE);
        }

        tracerHwBreakpointSetBits(&ctx->Dr7, 16 | (index << 2), 2, (int)cond);
        tracerHwBreakpointSetBits(&ctx->Dr7, 18 | (index << 2), 2, length);
        tracerHwBreakpointSetBits(&ctx->Dr7, index << 1, 1, 1);

        return index;

    } else {
        tracerCoreSetLastError(eTracerErrorOutOfResources);
    }

    return -1;
}

static TracerHandle tracerSetHwBreakpointOnForeignThread(void* address, int length, int threadId, TracerHwBpCond cond) {
    if (!address || threadId == (int)GetCurrentThreadId()) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return NULL;
    }

    switch (length) {
    case 1: length = 0; break;
    case 2: length = 1; break;
    case 4: length = 3; break;
    default:
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return NULL;
    }

    DWORD accessFlags = THREAD_GET_CONTEXT
        | THREAD_SET_CONTEXT
        | THREAD_QUERY_INFORMATION
        | THREAD_SUSPEND_RESUME;

    HANDLE thread = OpenThread(accessFlags, FALSE, (DWORD)threadId);

    if (!thread) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
        return NULL;
    }

    TracerBool suspended = (SuspendThread(thread) != (DWORD)-1);

    TracerHandle result = NULL;

    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if (GetThreadContext(thread, &ctx)) {

        // Find first available hardware register index
        int index = -1;

        if (!ctx.Dr0 && !tracerHwBreakpointGetBits(ctx.Dr7, 0 << 1, 1)) {
            index = 0;
        } else if (!ctx.Dr1 && !tracerHwBreakpointGetBits(ctx.Dr7, 1 << 1, 1)) {
            index = 1;
        } else if (!ctx.Dr2 && !tracerHwBreakpointGetBits(ctx.Dr7, 2 << 1, 1)) {
            index = 2;
        } else if (!ctx.Dr3 && !tracerHwBreakpointGetBits(ctx.Dr7, 3 << 1, 1)) {
            index = 3;
        }

        if (index >= 0) {

            TracerHwBreakpoint* breakpoint = (TracerHwBreakpoint*)
                malloc(sizeof(TracerHwBreakpoint));

            if (breakpoint) {
                breakpoint->mIndex = index;
                breakpoint->mThreadId = threadId;
                breakpoint->mNextLink = NULL;

                switch (index) {
                case 0: ctx.Dr0 = (uintptr_t)address; break;
                case 1: ctx.Dr1 = (uintptr_t)address; break;
                case 2: ctx.Dr2 = (uintptr_t)address; break;
                case 3: ctx.Dr3 = (uintptr_t)address; break;
                default: assert(FALSE);
                }

                tracerHwBreakpointSetBits(&ctx.Dr7, 16 | (index << 2), 2, (int)cond);
                tracerHwBreakpointSetBits(&ctx.Dr7, 18 | (index << 2), 2, length);
                tracerHwBreakpointSetBits(&ctx.Dr7, index << 1, 1, 1);

                if (SetThreadContext(thread, &ctx)) {
                    result = (TracerHandle)breakpoint;
                } else {
                    free(breakpoint);
                    tracerCoreSetLastError(eTracerErrorSystemCall);
                }

            } else {
                tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
            }

        } else {
            tracerCoreSetLastError(eTracerErrorOutOfResources);
        }

    } else {
        tracerCoreSetLastError(eTracerErrorSystemCall);
    }

    if (suspended) {
        ResumeThread(thread);
    }

    CloseHandle(thread);
    return result;
}

static TracerBool tracerRemoveHwBreakpointOnForeignThread(TracerHandle handle) {
    TracerHwBreakpoint* breakpoint = (TracerHwBreakpoint*)handle;

    if (!breakpoint || breakpoint->mThreadId == (int)GetCurrentThreadId()) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    DWORD accessFlags = THREAD_GET_CONTEXT
        | THREAD_SET_CONTEXT
        | THREAD_QUERY_INFORMATION
        | THREAD_SUSPEND_RESUME;

    TracerBool result = eTracerFalse;

    HANDLE thread = OpenThread(accessFlags, FALSE, breakpoint->mThreadId);

    if (thread) {
        TracerBool suspended = (SuspendThread(thread) != (DWORD)-1);

        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

        if (GetThreadContext(thread, &ctx)) {

            switch (breakpoint->mIndex) {
            case 0: ctx.Dr0 = 0; break;
            case 1: ctx.Dr1 = 0; break;
            case 2: ctx.Dr2 = 0; break;
            case 3: ctx.Dr3 = 0; break;
            default: assert(FALSE);
            }

            // Clear enabled bit for this breakpoint
            tracerHwBreakpointSetBits(&ctx.Dr7, breakpoint->mIndex << 1, 1, 0);

            if (SetThreadContext(thread, &ctx)) {
                result = eTracerTrue;
            } else {
                tracerCoreSetLastError(eTracerErrorSystemCall);
            }
        } else {
            tracerCoreSetLastError(eTracerErrorSystemCall);
        }

        if (suspended) {
            ResumeThread(thread);
        }

        CloseHandle(thread);

    } else {
        // The thread probably finished execution, return success
        result = eTracerTrue;
    }

    free(breakpoint);
    return result;
}

typedef struct TracerSetHwBreakpoint {
    void*                       mAddress;
    int                         mLength;
    int                         mThreadId;
    TracerHwBpCond              mCondition;
    TracerHandle                mBreakpoint;
} TracerSetHwBreakpoint;

static DWORD WINAPI tracerSetHwBreakpointOnCurrentThread(LPVOID parameter) {
    TracerSetHwBreakpoint* args = (TracerSetHwBreakpoint*)parameter;

    args->mBreakpoint = tracerSetHwBreakpointOnForeignThread(
        args->mAddress, args->mLength, args->mThreadId, args->mCondition);

    return TRUE;
}

TracerHandle tracerSetHwBreakpointOnThread(void* address, int length, int threadId, TracerHwBpCond cond) {
    if (!address) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return NULL;
    }

    TracerHandle result = NULL;

    int currentTid = (int)GetCurrentThreadId();

    if (threadId <= 0) {
        threadId = currentTid;
    }

    if (threadId == currentTid) {

        // If we want to set a breakpoint on the currently executing thread, we need to spawn a second thread
        // to be able to modify the registers of this thread. This is because calling GetThreadContext or
        // SetThreadContext is only valid for foreign threads.

        TracerSetHwBreakpoint setHwBp = {
            /* mAddress               = */ address,
            /* mLength                = */ length,
            /* mThreadId              = */ threadId,
            /* mCondition             = */ cond,
            /* mBreakpoint            = */ NULL,
        };

        HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)
            tracerSetHwBreakpointOnCurrentThread, (LPVOID)&setHwBp, 0, NULL);

        if (thread) {

            if (WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0) {

                // The new thread will store his result in the structure that we pass for the arguments
                result = setHwBp.mBreakpoint;

            } else {
                tracerCoreSetLastError(eTracerErrorWaitIncomplete);
            }

            CloseHandle(thread);

        } else {
            tracerCoreSetLastError(eTracerErrorSystemCall);
        }

    } else {
        result = tracerSetHwBreakpointOnForeignThread(address, length, threadId, cond);
    }

    return result;
}

TracerHandle tracerSetHwBreakpointGlobal(void* address, int length, TracerHwBpCond cond) {
    if (!address) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return NULL;
    }

    TracerHwBreakpoint* breakpoint = NULL;

    DWORD processId = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, processId);

    if (snapshot != INVALID_HANDLE_VALUE) {

        THREADENTRY32 entry;
        entry.dwSize = sizeof(entry);

        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID != processId) {
                    // The snapshot contained a process ID that we aren't even looking for
                    continue;
                }

                TracerHandle handle = tracerSetHwBreakpointOnThread(
                    address, length, (int)entry.th32ThreadID, cond);

                if (handle) {
                    TracerHwBreakpoint* current = (TracerHwBreakpoint*)handle;
                    current->mNextLink = breakpoint;

                    breakpoint = current;
                }

            } while (Thread32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);

    } else {
        tracerCoreSetLastError(eTracerErrorSystemCall);
    }

    return (TracerHandle)breakpoint;
}

static DWORD WINAPI tracerRemoveHwBreakpointOnCurrentThread(LPVOID parameter) {
    return tracerRemoveHwBreakpointOnForeignThread((TracerHandle)parameter);
}

TracerBool tracerRemoveHwBreakpoint(TracerHandle handle) {
    TracerHwBreakpoint* breakpoint = (TracerHwBreakpoint*)handle;

    if (!breakpoint) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    TracerBool result = eTracerTrue;

    do {

        // Each breakpoint handle can involve multiple threads, so we need to remove the breakpoint on each of them
        TracerHwBreakpoint* next = breakpoint->mNextLink;
        breakpoint->mNextLink = NULL;

        if (breakpoint->mThreadId == -1) {
            // This type of breakpoint was added manually by using a provided CONTEXT structure. It can only
            // be removed using tracerRemoveHwBreakpointOnContext.

            tracerCoreSetLastError(eTracerErrorInvalidArgument);
            return eTracerFalse;
        }

        if (breakpoint->mThreadId == (int)GetCurrentThreadId()) {

            // If we want to set a breakpoint on the currently executing thread, we need to spawn a second thread
            // to be able to modify the registers of this thread. This is because calling GetThreadContext or
            // SetThreadContext is only valid for foreign threads.

            HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)
                tracerRemoveHwBreakpointOnCurrentThread, (LPVOID)breakpoint, 0, NULL);

            if (thread) {

                if (WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0) {

                    DWORD exitCode = 0;

                    if (!GetExitCodeThread(thread, &exitCode) || exitCode == 0) {
                        result = eTracerFalse;
                    }

                } else {
                    result = eTracerFalse;
                    tracerCoreSetLastError(eTracerErrorWaitIncomplete);
                }

                CloseHandle(thread);

            } else {
                result = eTracerFalse;
                tracerCoreSetLastError(eTracerErrorSystemCall);
            }

        } else {
            if (!tracerRemoveHwBreakpointOnForeignThread((TracerHandle)breakpoint)) {
                result = eTracerFalse;
            }
        }

        breakpoint = next;

    } while (breakpoint);

    return result;
}

TracerBool tracerRemoveHwBreakpointOnContext(TracerHandle handle, PCONTEXT ctx) {
    TracerHwBreakpoint* breakpoint = (TracerHwBreakpoint*)handle;

    if (!breakpoint) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    TracerBool result = eTracerTrue;

    do {

        // Each breakpoint handle can involve multiple threads, so we need to remove the breakpoint on each of them
        TracerHwBreakpoint* next = breakpoint->mNextLink;
        breakpoint->mNextLink = NULL;

        if (breakpoint->mThreadId == -1 ||
            breakpoint->mThreadId == (int)GetCurrentThreadId()) {

            switch (breakpoint->mIndex) {
            case 0: ctx->Dr0 = 0; break;
            case 1: ctx->Dr1 = 0; break;
            case 2: ctx->Dr2 = 0; break;
            case 3: ctx->Dr3 = 0; break;
            default: assert(FALSE);
            }

            // Clear enabled bit for this breakpoint
            tracerHwBreakpointSetBits(&ctx->Dr7, breakpoint->mIndex << 1, 1, 0);

            free(breakpoint);

        } else {
            if (!tracerRemoveHwBreakpointOnForeignThread((TracerHandle)breakpoint)) {
                result = eTracerFalse;
            }
        }

        breakpoint = next;

    } while (breakpoint);

    return result;
}