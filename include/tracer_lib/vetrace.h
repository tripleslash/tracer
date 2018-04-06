#ifndef TLIB_VETRACE_H
#define TLIB_VETRACE_H

#include <tracer_lib/trace.h>

#define ZYDIS_STATIC_DEFINE

#include <Zydis/Zydis.h>

typedef struct TracerActiveTrace {
    void*                       mStartAddress;
    uintptr_t                   mBaseOfCode;
    uintptr_t                   mSizeOfCode;
    int                         mThreadId;
    TracerHandle                mBreakpoint;
    struct TracerActiveTrace*   mNextLink;
} TracerActiveTrace;

typedef struct TracerVeTraceContext {
    TracerTraceContext          mBaseContext;
    TracerHandle                mAddVehHandle;
    TracerHandle                mSharedRWQueue;
    ZydisDecoder                mDecoder;
    ZydisFormatter              mFormatter;
    TracerActiveTrace*          mActiveTraces;
    int                         mMaxCallDepth;
} TracerVeTraceContext;

TracerContext* tracerCreateVeTraceContext(int type, int size, TracerHandle traceQueue);

void tracerCleanupVeTraceContext(TracerContext* ctx);

#endif
