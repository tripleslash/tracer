
#include <tracer_lib/trace.h>

#include <assert.h>

TracerContext* tracerCreateTraceContext(int type, int size) {
    assert(size >= sizeof(TracerTraceContext));

    TracerContext* ctx = tracerCoreCreateContext(type | eTracerTraceContext, size);
    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupTraceContext;

    TracerTraceContext* trace = (TracerTraceContext*)ctx;

    return ctx;
}

void tracerCleanupTraceContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContext)) {
        return;
    }

    TracerTraceContext* trace = (TracerTraceContext*)ctx;
    // ...

    tracerCoreCleanupContext(ctx);
}

TracerBool tracerTraceStart(TracerContext* ctx, void* address, int threadId) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContext)) {
        return eTracerFalse;
    }
    TracerTraceContext* trace = (TracerTraceContext*)ctx;
    TLIB_METHOD_CHECK_SUPPORT(trace->mStartTrace, eTracerFalse);
    return trace->mStartTrace(ctx, address, threadId);
}

TracerBool tracerTraceStop(TracerContext* ctx, void* address, int threadId) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContext)) {
        return eTracerFalse;
    }
    TracerTraceContext* trace = (TracerTraceContext*)ctx;
    TLIB_METHOD_CHECK_SUPPORT(trace->mStopTrace, eTracerFalse);
    return trace->mStopTrace(ctx, address, threadId);
}
