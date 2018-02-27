#ifndef TLIB_PROCESS_REMOTE_H
#define TLIB_PROCESS_REMOTE_H

#include <tracer_lib/process.h>

typedef struct TracerRemoteProcessContext {
    TracerProcessContext            mBaseContext;
} TracerRemoteProcessContext;

TracerContext* tracerCreateRemoteProcessContext(int type, int size, int pid);

void tracerCleanupRemoteProcessContext(TracerContext* ctx);

#endif
