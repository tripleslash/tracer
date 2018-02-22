#include "process_local.h"
#include "memory_local.h"

#include <stdio.h>
#include <assert.h>

static TracerBool tracerProcessLocalInit(TracerContext* ctx);

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx);

static TracerContext* gTracerLocalProcessContext = NULL;

TracerContext* tracerCreateLocalProcessContext(int type, int size) {
    assert(size >= sizeof(TracerLocalProcessContext));
    assert(gTracerLocalProcessContext == NULL);

    int pid = (int)GetCurrentProcessId();

    TracerContext* ctx = tracerCreateProcessContext(
        type | eTracerProcessContextLocal, size, pid);

    if (!ctx) {
        return NULL;
    }

    // Set a global reference to the local process context
    gTracerLocalProcessContext = ctx;

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupLocalProcessContext;

    TracerProcessContext* process = (TracerProcessContext*)ctx;

    process->mMemoryContext = tracerCreateLocalMemoryContext(
        eTracerMemoryContextLocal, sizeof(TracerLocalMemoryContext));

    if (!process->mMemoryContext) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    if (!tracerProcessLocalInit(ctx)) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    return ctx;
}

void tracerCleanupLocalProcessContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContextLocal)) {
        return;
    }
    tracerProcessLocalShutdown(ctx);
    tracerCleanupProcessContext(ctx);

    gTracerLocalProcessContext = NULL;
}

TracerContext* tracerGetLocalProcessContext(void) {
    return gTracerLocalProcessContext;
}

static TracerBool tracerProcessLocalInit(TracerContext* ctx) {
    return eTracerTrue;
}

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx) {
    return eTracerTrue;
}
