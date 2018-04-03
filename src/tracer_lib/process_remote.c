
#include <tracer_lib/process_remote.h>
#include <tracer_lib/memory_remote.h>
#include <tracer_lib/rwqueue.h>

#include <stdio.h>
#include <assert.h>

static TracerBool tracerProcessRemoteInit(TracerContext* ctx);

static TracerBool tracerProcessRemoteShutdown(TracerContext* ctx);

static TracerBool tracerProcessRemoteCreateFileMapping(TracerContext* ctx,
    TracerHandle* localMapping, TracerHandle* remoteMapping);

static TracerBool tracerProcessRemoteStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace);

static TracerBool tracerProcessRemoteStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace);

TracerContext* tracerCreateRemoteProcessContext(int type, int size, int pid) {
    assert(size >= sizeof(TracerRemoteProcessContext));
    assert(pid >= 0);

    TracerContext* ctx = tracerCreateProcessContext(
        type | eTracerProcessContextRemote, size, pid);

    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupRemoteProcessContext;

    TracerHandle localMapping, remoteMapping;

    if (!tracerProcessRemoteCreateFileMapping(ctx, &localMapping, &remoteMapping)) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    TracerProcessContext* process = (TracerProcessContext*)ctx;
    process->mSharedMemoryHandle = localMapping;
    process->mStartTrace = tracerProcessRemoteStartTrace;
    process->mStopTrace = tracerProcessRemoteStopTrace;

    process->mMemoryContext = tracerCreateRemoteMemoryContext(
        eTracerMemoryContextRemote,
        sizeof(TracerRemoteMemoryContext),
        pid,
        remoteMapping);

    if (!process->mMemoryContext) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    process->mMappedView = MapViewOfFile(process->mSharedMemoryHandle,
        FILE_MAP_ALL_ACCESS, 0, 0, TLIB_QUEUE_SIZE_IN_BYTES);

    if (!process->mMappedView) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    process->mSharedRWQueue = tracerCreateRWQueue(process->mMappedView,
        TLIB_QUEUE_SIZE_IN_BYTES, 1);

    if (!process->mSharedRWQueue) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    if (!tracerProcessRemoteInit(ctx)) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    return ctx;
}

void tracerCleanupRemoteProcessContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContextRemote)) {
        return;
    }
    tracerProcessRemoteShutdown(ctx);
    tracerCleanupProcessContext(ctx);
}

static TracerBool tracerProcessRemoteInit(TracerContext* ctx) {
    return eTracerTrue;
}

static TracerBool tracerProcessRemoteShutdown(TracerContext* ctx) {
    return eTracerTrue;
}

static TracerBool tracerProcessRemoteCreateFileMapping(TracerContext* ctx,
    TracerHandle* localMapping, TracerHandle* remoteMapping) {

    // This creates a shared memory segment between the local and the remote process.
    // Within this shared memory segment an RWQueue will be created in which the trace results are pushed

    TracerProcessContext* process = (TracerProcessContext*)ctx;

    TracerBool success = eTracerFalse;

    HANDLE fileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
        TLIB_QUEUE_SIZE_IN_BYTES, NULL);

    if (fileMapping) {

        HANDLE remoteProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, process->mProcessId);
        if (remoteProcess) {

            HANDLE remoteFileMapping = NULL;

            if (DuplicateHandle(GetCurrentProcess(), fileMapping, remoteProcess,
                &remoteFileMapping, DUPLICATE_SAME_ACCESS, FALSE, DUPLICATE_SAME_ACCESS))
            {

                *localMapping = fileMapping;
                *remoteMapping = remoteFileMapping;

                success = eTracerTrue;

            } else {
                tracerCoreSetLastError(eTracerErrorSystemCall);
            }

            CloseHandle(remoteProcess);

        } else {
            tracerCoreSetLastError(eTracerErrorInsufficientPermission);
        }

        if (!success) {
            CloseHandle(fileMapping);
        }

    } else {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
    }

    return success;
}

static TracerBool tracerProcessRemoteStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace) {
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    return (TracerBool)tracerMemoryRemoteCallLocalExport(process->mMemoryContext, "tracerStartTraceEx", (const TracerStruct*)startTrace);
}

static TracerBool tracerProcessRemoteStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace) {
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    return (TracerBool)tracerMemoryRemoteCallLocalExport(process->mMemoryContext, "tracerStopTraceEx", (const TracerStruct*)stopTrace);
}
