#ifndef TLIB_VETRACE_H
#define TLIB_VETRACE_H

#include <tracer_lib/trace.h>

#define ZYDIS_STATIC_DEFINE

#include <Zydis/Zydis.h>
#pragma comment(lib, "Zydis/Zydis.lib")

typedef enum TracerVeTraceNodeType {
    eTracerNodeTypeBranch       = 0,
    eTracerNodeTypeCall         = 1,
    eTracerNodeTypeReturn       = 2,
} TracerVeTraceNodeType;

typedef struct TracerVeTraceNode {
    struct TracerVeTraceNode*   mPrev;
    struct TracerVeTraceNode*   mNext;

    struct TracerVeTraceNode*   mParent;
    struct TracerVeTraceNode*   mFirstChild;

    TracerVeTraceNodeType       mType;
} TracerVeTraceNode;

typedef struct TracerVeTraceContext {
    TracerTraceContext          mBaseContext;
    TracerHandle                mAddVehHandle;
    ZydisDecoder                mDecoder;
    TracerVeTraceNode*          mActiveTrace;
    TracerVeTraceNode*          mLastNode;
    uintptr_t                   mWow64CpuDllStart;
    uintptr_t                   mWow64CpuDllEnd;
} TracerVeTraceContext;

TracerContext* tracerCreateVeTraceContext(int type, int size);

void tracerCleanupVeTraceContext(TracerContext* ctx);

#endif
