#include "process_remote.h"
#include "memory_remote.h"

#include <stdio.h>
#include <assert.h>

static TracerBool tracerProcessRemoteInit(TracerContext* ctx);

static TracerBool tracerProcessRemoteShutdown(TracerContext* ctx);

TracerContext* tracerCreateRemoteProcessContext(int type, int size, int pid) {
    assert(size >= sizeof(TracerRemoteProcessContext));
    assert(pid >= 0);

    TracerContext* ctx = tracerCreateProcessContext(
        type | eTracerProcessContextRemote, size, pid);

    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupRemoteProcessContext;

    TracerProcessContext* process = (TracerProcessContext*)ctx;

    process->mMemoryContext = tracerCreateRemoteMemoryContext(
        eTracerMemoryContextRemote, sizeof(TracerRemoteMemoryContext), pid);

    if (!process->mMemoryContext) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    if (!tracerProcessRemoteInit(ctx)) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    return ctx;
}

void tracerCleanupRemoteProcessContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContextRemote)) {
        return;
    }
    tracerProcessRemoteShutdown(ctx);
    tracerCleanupProcessContext(ctx);
}

static TracerBool tracerProcessRemoteInit(TracerContext* ctx) {
    return eTracerTrue;
}

static TracerBool tracerProcessRemoteShutdown(TracerContext* ctx) {
    return eTracerTrue;
}
