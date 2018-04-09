#ifndef TLIB_VETRACE_H
#define TLIB_VETRACE_H

#include <tracer_lib/trace.h>

typedef struct TracerActiveTrace {
    void*                       mStartAddress;
    uintptr_t                   mBaseOfCode;
    uintptr_t                   mSizeOfCode;
    int                         mThreadId;
    int                         mMaxTraceDepth;
    int                         mLifetime;
    TracerHandle                mBreakpoint;
    struct TracerActiveTrace*   mNextLink;
} TracerActiveTrace;

typedef struct TracerVeTraceContext {
    TracerTraceContext          mBaseContext;
    TracerHandle                mAddVehHandle;
    TracerHandle                mSharedRWQueue;
    TracerActiveTrace*          mActiveTraces;
    TracerActiveTrace* volatile mCurrentTrace;
    CRITICAL_SECTION            mTraceCritSect;
} TracerVeTraceContext;

TracerContext* tracerCreateVeTraceContext(int type, int size, TracerHandle traceQueue);

void tracerCleanupVeTraceContext(TracerContext* ctx);

#endif
