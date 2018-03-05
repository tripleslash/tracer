#ifndef TLIB_HWBP_H
#define TLIB_HWBP_H

#include <tracer_lib/core.h>

typedef enum TracerHwBpCond {
    eTracerBpCondExecute        = 0,
    eTracerBpCondWrite          = 1,
    eTracerBpCondRead           = 2,
    eTracerBpCondReadWrite      = 3,
} TracerHwBpCond;

int tracerHwBreakpointGetBits(uintptr_t dw, int lowBit, int bits);

void tracerHwBreakpointSetBits(uintptr_t* dw, int lowBit, int bits, int newValue);

int tracerSetHwBreakpointOnContext(void* address, int length, PCONTEXT ctx, TracerHwBpCond cond);

TracerHandle tracerSetHwBreakpointOnThread(void* address, int length, int threadId, TracerHwBpCond cond);

TracerHandle tracerSetHwBreakpointGlobal(void* address, int length, TracerHwBpCond cond);

TracerBool tracerRemoveHwBreakpoint(TracerHandle breakpoint);

#endif
