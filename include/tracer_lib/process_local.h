#ifndef TLIB_PROCESS_LOCAL_H
#define TLIB_PROCESS_LOCAL_H

#include <tracer_lib/process.h>

#define ZYDIS_STATIC_DEFINE

#include <Zydis/Zydis.h>

typedef struct TracerLocalProcessContext {
    TracerProcessContext            mBaseContext;
    TracerContext*                  mTraceContext;

    ZydisDecoder                    mDecoder;
    ZydisFormatter                  mFormatter;
} TracerLocalProcessContext;

TracerContext* tracerCreateLocalProcessContext(int type, int size, TracerHandle sharedMemoryHandle);

void tracerCleanupLocalProcessContext(TracerContext* ctx);

TracerContext* tracerGetLocalProcessContext(void);

#endif
