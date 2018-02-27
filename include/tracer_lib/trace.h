#ifndef TLIB_TRACE_H
#define TLIB_TRACE_H

#include <tracer_lib/core.h>

typedef struct TracerTraceContext {
    TracerBaseContext           mBaseContext;

    TracerBool(*mStartTrace)(TracerContext* ctx, void* address, int threadId);

    TracerBool(*mStopTrace)(TracerContext* ctx, void* address, int threadId);
} TracerTraceContext;

TracerContext* tracerCreateTraceContext(int type, int size);

void tracerCleanupTraceContext(TracerContext* ctx);

TracerBool tracerTraceStart(TracerContext* ctx, void* address, int threadId);

TracerBool tracerTraceStop(TracerContext* ctx, void* address, int threadId);

#endif
