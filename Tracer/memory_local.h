#ifndef TLIB_MEMORY_LOCAL_H
#define TLIB_MEMORY_LOCAL_H

#include "memory.h"

typedef struct TracerLocalMemoryContext {
    TracerMemoryContext             mBaseContext;
} TracerLocalMemoryContext;

TracerContext* tracerCreateLocalMemoryContext(int type, int size);

void tracerCleanupLocalMemoryContext(TracerContext* ctx);

#endif
