#ifndef TLIB_MEMORY_REMOTE_H
#define TLIB_MEMORY_REMOTE_H

#include <tracer_lib/memory.h>

typedef struct TracerRemoteMemoryContext {
    TracerMemoryContext             mBaseContext;
} TracerRemoteMemoryContext;

TracerContext* tracerCreateRemoteMemoryContext(int type, int size, int pid);

void tracerCleanupRemoteMemoryContext(TracerContext* ctx);

#endif
