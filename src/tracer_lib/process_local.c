
#include <tracer_lib/process_local.h>
#include <tracer_lib/memory_local.h>
#include <tracer_lib/vetrace.h>
#include <tracer_lib/rwqueue.h>

#include <assert.h>

static TracerBool tracerProcessLocalInit(TracerContext* ctx);

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx);

static TracerBool tracerProcessLocalStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace);

static TracerBool tracerProcessLocalStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace);

static TracerContext* gTracerLocalProcessContext = NULL;

TracerContext* tracerCreateLocalProcessContext(int type, int size, TracerHandle sharedMemoryHandle) {
    assert(size >= sizeof(TracerLocalProcessContext));
    assert(gTracerLocalProcessContext == NULL);

    int pid = (int)GetCurrentProcessId();

    TracerContext* ctx = tracerCreateProcessContext(
        type | eTracerProcessContextLocal, size, pid);

    if (!ctx) {
        return NULL;
    }

    // Set a global reference to the local process context
    gTracerLocalProcessContext = ctx;

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupLocalProcessContext;

    TracerProcessContext* process = (TracerProcessContext*)ctx;
    process->mSharedMemoryHandle = sharedMemoryHandle;
    process->mStartTrace = tracerProcessLocalStartTrace;
    process->mStopTrace = tracerProcessLocalStopTrace;

    process->mMemoryContext = tracerCreateLocalMemoryContext(
        eTracerMemoryContextLocal, sizeof(TracerLocalMemoryContext));

    if (!process->mMemoryContext) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    if (process->mSharedMemoryHandle) {
        process->mMappedView = MapViewOfFile(process->mSharedMemoryHandle,
            FILE_MAP_ALL_ACCESS, 0, 0, TLIB_SHARED_MEMORY_SIZE);

        if (!process->mMappedView) {
            tracerCoreDestroyContext(ctx);
            return NULL;
        }
    }

    process->mSharedRWQueue = tracerCreateRWQueue(process->mMappedView,
        TLIB_SHARED_MEMORY_SIZE, sizeof(TracerTracedInstruction));

    if (!process->mSharedRWQueue) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    if (!tracerProcessLocalInit(ctx)) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    return ctx;
}

void tracerCleanupLocalProcessContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContextLocal)) {
        return;
    }
    tracerProcessLocalShutdown(ctx);
    tracerCleanupProcessContext(ctx);

    gTracerLocalProcessContext = NULL;
}

TracerContext* tracerGetLocalProcessContext(void) {
    return gTracerLocalProcessContext;
}

static TracerBool tracerProcessLocalInit(TracerContext* ctx) {
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    TracerLocalProcessContext* local = (TracerLocalProcessContext*)ctx;

    local->mTraceContext = tracerCreateVeTraceContext(eTracerTraceContextVEH,
        sizeof(TracerVeTraceContext), process->mSharedRWQueue);

    if (!local->mTraceContext) {
        return eTracerFalse;
    }

    return eTracerTrue;
}

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    if (process->mTraceContext) {
        tracerCoreDestroyContext(process->mTraceContext);
        process->mTraceContext = NULL;
    }

    return eTracerTrue;
}

static TracerBool tracerProcessLocalStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;
    return tracerTraceStart(process->mTraceContext, startTrace->mAddress, startTrace->mThreadId);
}

static TracerBool tracerProcessLocalStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;
    return tracerTraceStop(process->mTraceContext, stopTrace->mAddress, stopTrace->mThreadId);
}
