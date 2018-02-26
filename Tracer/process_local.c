#include "process_local.h"
#include "memory_local.h"
#include "vetrace.h"

#include <assert.h>

static TracerBool tracerProcessLocalInit(TracerContext* ctx);

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx);

static TracerBool tracerProcessLocalStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace);

static TracerBool tracerProcessLocalStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace);

static TracerContext* gTracerLocalProcessContext = NULL;

TracerContext* tracerCreateLocalProcessContext(int type, int size) {
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
    process->mStartTrace = tracerProcessLocalStartTrace;
    process->mStopTrace = tracerProcessLocalStopTrace;

    process->mMemoryContext = tracerCreateLocalMemoryContext(
        eTracerMemoryContextLocal, sizeof(TracerLocalMemoryContext));

    if (!process->mMemoryContext) {
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
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    process->mTraceContext = tracerCreateVeTraceContext(eTracerTraceContextVEH, sizeof(TracerVeTraceContext));
    if (!process->mTraceContext) {
        return eTracerFalse;
    }

    return eTracerTrue;
}

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    tracerCoreDestroyContext(process->mTraceContext);
    process->mTraceContext = NULL;

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
