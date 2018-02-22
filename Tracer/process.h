#ifndef TLIB_PROCESS_H
#define TLIB_PROCESS_H

#include "core.h"

typedef struct TracerProcessContext {
    TracerBaseContext           mBaseContext;
    int                         mProcessId;
    TracerContext*              mMemoryContext;
} TracerProcessContext;

TracerContext* tracerCreateProcessContext(int type, int size, int pid);

void tracerCleanupProcessContext(TracerContext* ctx);

int tracerProcessGetPid(TracerContext* ctx);

TracerContext* tracerProcessGetMemoryContext(TracerContext* ctx);

#endif
