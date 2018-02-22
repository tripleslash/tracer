#include "core.h"

#include "process_local.h"
#include "process_remote.h"

#include <stdio.h>

static TracerBool tracerDetachProcessCallback(TracerContext* ctx, void* param) {
    int processId = tracerProcessGetPid(ctx);
    if (!processId) {
        return eTracerFalse;
    }

    if (ctx == tracerCoreGetProcessContext()) {
        tracerCoreSetProcessContext(NULL);
    }

    tracerCoreSetContextForPID(processId, NULL);
    tracerCoreDestroyContext(ctx);
    return eTracerTrue;
}

static TracerBool tracerSetHotkeysCallback(TracerContext* ctx, void* param) {
    //return tracerProcessSetHotkeys(ctx, (TracerHotkeySet*)param);
    return eTracerFalse;
}

/*
 *
 * Exported API functions
 *
 */

static int gHasSeDebugPrivilege = 0;

TLIB_API TracerBool TLIB_CALL tracerInit(int version) {
    TracerInit init = {
        /* mSizeOfStruct            = */ sizeof(TracerInit),
        /* mVersion                 = */ version,
        /* mAcquireSeDebugPrivilege = */ eTracerTrue
    };
    return tracerInitEx(&init);
}

TLIB_API TracerBool TLIB_CALL tracerInitEx(TracerInit* init) {
    tracerCoreSetLastError(eTracerErrorSuccess);

    if (!init || init->mSizeOfStruct < sizeof(TracerInit)) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    if (init->mVersion < 1 || init->mVersion > TLIB_VERSION) {
        tracerCoreSetLastError(eTracerErrorWrongVersion);
        return eTracerFalse;
    }

    if (init->mAcquireSeDebugPrivilege) {
        TracerHandle process = (TracerHandle)GetCurrentProcess();
        gHasSeDebugPrivilege = tracerCoreSetPrivilege(process, SE_DEBUG_NAME, eTracerTrue);
    }

    return eTracerTrue;
}

TLIB_API TracerBool TLIB_CALL tracerShutdown() {
    TracerShutdown shutdown = {
        /* mSizeOfStruct    = */ sizeof(TracerShutdown)
    };
    return tracerShutdownEx(&shutdown);
}

TLIB_API TracerBool TLIB_CALL tracerShutdownEx(TracerShutdown* shutdown) {
    tracerCoreSetLastError(eTracerErrorSuccess);

    if (!shutdown || shutdown->mSizeOfStruct < sizeof(TracerShutdown)) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    TracerBool result = tracerDetachProcess(NULL);

    if (gHasSeDebugPrivilege) {
        TracerHandle process = (TracerHandle)GetCurrentProcess();
        gHasSeDebugPrivilege = !tracerCoreSetPrivilege(process, SE_DEBUG_NAME, eTracerFalse);
    }

    return result;
}

TLIB_API int TLIB_CALL tracerGetVersion() {
    return TLIB_VERSION;
}

TLIB_API TracerError TLIB_CALL tracerGetLastError() {
    return tracerCoreGetLastError();
}

TLIB_API const char* TLIB_CALL tracerErrorToString(TracerError error) {
    switch (error) {
    case eTracerErrorSuccess:
        return "The operation completed successfully.";
    case eTracerErrorWrongVersion:
        return "The library version does not match the representation in this header file.";
    case eTracerErrorNotImplemented:
        return "The operation failed because it is not currently implemented.";
    case eTracerErrorInvalidArgument:
        return "The operation failed due to an invalid argument.";
    case eTracerErrorInvalidProcess:
        return "The operation failed due to an invalid process id.";
    case eTracerErrorInvalidHandle:
        return "The operation failed due to an invalid handle.";
    case eTracerErrorInsufficientPermission:
        return "The operation failed due to insufficient permission.";
    case eTracerErrorNotEnoughMemory:
        return "The operation failed because there is not enough memory available.";
    case eTracerErrorSystemCall:
        return "The operation failed because a system call returned an error.";
    case eTracerErrorWaitTimeout:
        return "The operation timed out due to a user specified timeout parameter.";
    case eTracerErrorWaitIncomplete:
        return "The operation failed because one of the wait handles returned an error.";
    case eTracerErrorRemoteInterop:
        return "The operation failed because the remote end returned an error.";
    case eTracerErrorPatternsNotFound:
        return "The operation failed because one of the patterns could not be found.";
    }
    return "Unknown error.";
}

TLIB_API TracerContext* TLIB_CALL tracerAttachProcess(int pid) {
    TracerAttachProcess attach = {
        /* mSizeOfStruct       = */ sizeof(TracerAttachProcess),
        /* mProcessId          = */ pid
    };
    return tracerAttachProcessEx(&attach);
}

TLIB_API TracerContext* TLIB_CALL tracerAttachProcessEx(TracerAttachProcess* attach) {
    tracerCoreSetLastError(eTracerErrorSuccess);

    if (!attach || attach->mSizeOfStruct < sizeof(TracerAttachProcess)) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return NULL;
    }

    int pid = attach->mProcessId;
    int currentPid = (int)GetCurrentProcessId();

    if (pid <= 0) {
        pid = currentPid;
    }

    tracerCoreAcquireProcessContextLock();

    TracerContext* ctx = tracerCoreGetContextForPID(pid);
    if (!ctx) {
        if (pid == currentPid) {
            ctx = tracerCreateLocalProcessContext(eTracerProcessContextLocal, sizeof(TracerLocalProcessContext));
        } else {
            ctx = tracerCreateRemoteProcessContext(eTracerProcessContextRemote, sizeof(TracerRemoteProcessContext), pid);
        }

        tracerCoreSetContextForPID(pid, ctx);
    }

    tracerCoreReleaseProcessContextLock();
    return ctx;
}

TLIB_API TracerBool TLIB_CALL tracerDetachProcess(TracerContext* ctx) {
    tracerCoreSetLastError(eTracerErrorSuccess);

    TracerBool result = eTracerFalse;
    tracerCoreAcquireProcessContextLock();

    if (ctx) {
        result = tracerDetachProcessCallback(ctx, NULL);
    } else {
        result = tracerCoreEnumProcessContexts(tracerDetachProcessCallback, NULL, eTracerFalse);
    }

    tracerCoreReleaseProcessContextLock();
    return result;
}

TLIB_API void TLIB_CALL tracerSetProcessContext(TracerContext* ctx) {
    tracerCoreSetLastError(eTracerErrorSuccess);
    tracerCoreSetProcessContext(ctx);
}

TLIB_API TracerContext* TLIB_CALL tracerGetProcessContext() {
    tracerCoreSetLastError(eTracerErrorSuccess);
    return tracerCoreGetProcessContext();
}

TLIB_API TracerContext* TLIB_CALL tracerGetContextForPid(int pid) {
    tracerCoreSetLastError(eTracerErrorSuccess);

    int currentPid = (int)GetCurrentProcessId();

    if (pid <= 0) {
        pid = currentPid;
    }

    tracerCoreAcquireProcessContextLock();

    TracerContext* ctx = tracerCoreGetContextForPID(pid);

    tracerCoreReleaseProcessContextLock();
    return ctx;
}

TLIB_API TracerBool TLIB_CALL tracerSetHotkeys(TracerHotkeySet* hotkeys) {
    tracerCoreSetLastError(eTracerErrorSuccess);

    if (!hotkeys || hotkeys->mSizeOfStruct < sizeof(TracerHotkeySet)) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    TracerBool result = eTracerFalse;
    tracerCoreAcquireProcessContextLock();

    TracerContext* ctx = tracerCoreGetProcessContext();
    if (ctx) {
        // result = tracerProcessSetHotkeys(ctx, hotkeys);
    } else {
        result = tracerCoreEnumContexts(tracerSetHotkeysCallback,
            hotkeys, eTracerProcessContext, eTracerFalse);
    }

    tracerCoreReleaseProcessContextLock();
    return result;
}
