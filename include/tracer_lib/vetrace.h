#ifndef TLIB_VETRACE_H
#define TLIB_VETRACE_H

#include <tracer_lib/trace.h>

typedef struct TracerVeTraceContext {
    TracerTraceContext          mBaseContext;
    TracerHandle                mAddVehHandle;
} TracerVeTraceContext;

TracerContext* tracerCreateVeTraceContext(int type, int size);

void tracerCleanupVeTraceContext(TracerContext* ctx);

#endif