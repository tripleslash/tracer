#ifndef TLIB_PROCESS_LOCAL_H
#define TLIB_PROCESS_LOCAL_H

#include "process.h"

typedef struct TracerLocalProcessContext {
    TracerProcessContext            mBaseContext;
    TracerHotkeySet                 mHotkeys;
} TracerLocalProcessContext;

TracerContext* tracerCreateLocalProcessContext(int type, int size);

void tracerCleanupLocalProcessContext(TracerContext* ctx);

TracerContext* tracerGetLocalProcessContext(void);

#endif
