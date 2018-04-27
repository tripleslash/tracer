#ifndef TLIB_MEMORY_REMOTE_H
#define TLIB_MEMORY_REMOTE_H

#include <tracer_lib/memory.h>

typedef struct TracerRemoteMemoryContext {
    TracerMemoryContext             mBaseContext;
} TracerRemoteMemoryContext;

TracerContext* tracerCreateRemoteMemoryContext(int type, int size, int pid, TracerHandle sharedMemoryHandle);

void tracerCleanupRemoteMemoryContext(TracerContext* ctx);

int tracerMemoryRemoteCallLocalExport(TracerContext* ctx, const char* exportName, const TracerStruct* parameter);

int tracerMemoryRemoteCallLocalExportEx(TracerContext* ctx, const char* exportName, TracerStruct* parameter);

#endif
