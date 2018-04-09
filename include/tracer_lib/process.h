#ifndef TLIB_PROCESS_H
#define TLIB_PROCESS_H

#include <tracer_lib/core.h>
#include <tracer_lib/trace.h>

#define ZYDIS_STATIC_DEFINE

#include <Zydis/Zydis.h>

#define TLIB_IN_MEGABYTES                   1024*1024
#define TLIB_SHARED_MEMORY_SIZE             16*TLIB_IN_MEGABYTES

typedef struct TracerProcessContext {
    TracerBaseContext           mBaseContext;
    int                         mProcessId;
    TracerContext*              mMemoryContext;
    TracerHandle                mSharedMemoryHandle;
    TracerHandle                mSharedRWQueue;
    void*                       mMappedView;

    ZydisDecoder                mDecoder;
    ZydisFormatter              mFormatter;

    TracerBool(*mStartTrace)(TracerContext* ctx, const TracerStartTrace* startTrace);

    TracerBool(*mStopTrace)(TracerContext* ctx, const TracerStopTrace* stopTrace);
} TracerProcessContext;

TracerContext* tracerCreateProcessContext(int type, int size, int pid);

void tracerCleanupProcessContext(TracerContext* ctx);

int tracerProcessGetPid(TracerContext* ctx);

TracerContext* tracerProcessGetMemoryContext(TracerContext* ctx);

TracerBool tracerProcessStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace);

TracerBool tracerProcessStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace);

size_t tracerProcessFetchTraces(TracerContext* ctx, TracerTracedInstruction* outTraces, size_t maxElements);

TracerBool tracerProcessFormatInstruction(TracerContext* ctx, uintptr_t address, char* outBuffer, size_t bufferLength);

#endif
