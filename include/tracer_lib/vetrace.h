#ifndef TLIB_VETRACE_H
#define TLIB_VETRACE_H

#include <tracer_lib/trace.h>

#define ZYDIS_STATIC_DEFINE

#include <Zydis/Zydis.h>
#pragma comment(lib, "Zydis/Zydis.lib")

typedef enum TracerTracedInstructionType {
    eTracerNodeTypeBranch       = 0,
    eTracerNodeTypeCall         = 1,
    eTracerNodeTypeReturn       = 2,
} TracerTracedInstructionType;

typedef struct TracerTracedInstruction {
    int                         mTraceId;
    TracerTracedInstructionType mType;
    ZydisDecodedInstruction     mInstruction;
} TracerTracedInstruction;

typedef struct TracerActiveTrace {
    void*                       mStartAddress;
} TracerActiveTrace;

#define TLIB_MAX_ACTIVE_TRACES  4
#define TLIB_MAX_TRACE_LENGTH   8192

typedef struct TracerVeTraceContext {
    TracerTraceContext          mBaseContext;
    TracerHandle                mAddVehHandle;
    ZydisDecoder                mDecoder;
    uintptr_t                   mBaseOfCode;
    uintptr_t                   mSizeOfCode;
    TracerActiveTrace           mActiveTraces[TLIB_MAX_ACTIVE_TRACES];
    TracerTracedInstruction     mTraceArray[TLIB_MAX_TRACE_LENGTH];
    int                         mTraceLength;
    int                         mCallDepth;
    int                         mCurrentTraceId;
} TracerVeTraceContext;

TracerContext* tracerCreateVeTraceContext(int type, int size);

void tracerCleanupVeTraceContext(TracerContext* ctx);

#endif
