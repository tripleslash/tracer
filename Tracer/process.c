
#include "process.h"
#include <assert.h>

TracerContext* tracerCreateProcessContext(int type, int size, int pid) {
    assert(size >= sizeof(TracerProcessContext));

    TracerContext* ctx = tracerCoreCreateContext(type | eTracerProcessContext, size);
    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupProcessContext;

    TracerProcessContext* process = (TracerProcessContext*)ctx;
    process->mProcessId = pid;

    return ctx;
}

void tracerCleanupProcessContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return;
    }

    TracerProcessContext* process = (TracerProcessContext*)ctx;

    tracerCoreDestroyContext(process->mMemoryContext);
    process->mMemoryContext = NULL;

    tracerCoreCleanupContext(ctx);
}

int tracerProcessGetPid(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return 0;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    return process->mProcessId;
}

TracerContext* tracerProcessGetMemoryContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return NULL;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    return process->mMemoryContext;
}

TracerBool tracerProcessStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return eTracerFalse;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    TLIB_METHOD_CHECK_SUPPORT(process->mStartTrace, eTracerFalse);
    return process->mStartTrace(ctx, startTrace);
}

TracerBool tracerProcessStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return eTracerFalse;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    TLIB_METHOD_CHECK_SUPPORT(process->mStopTrace, eTracerFalse);
    return process->mStopTrace(ctx, stopTrace);
}
