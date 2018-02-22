#ifndef TLIB_MEMORY_H
#define TLIB_MEMORY_H

#include "core.h"

typedef struct TracerMemoryContext {
    TracerBaseContext           mBaseContext;
    int                         mProcessId;
    TracerHandle                mProcessHandle;
    TracerHandle                mModuleHandle;

    size_t(*mWriteMemory)(TracerContext* ctx, void* address, const void* buffer, size_t size);

    size_t(*mReadMemory)(TracerContext* ctx, const void* address, void* buffer, size_t size);

    void*(*mAllocMemory)(TracerContext* ctx, size_t size);

    void(*mFreeMemory)(TracerContext* ctx, void* address);

    TracerHandle(*mFindModule)(TracerContext* ctx, const tchar* dllName);

    const uint8_t*(*mSearchPattern)(TracerContext* ctx, const uint8_t* haystack, size_t haystackSize, const uint8_t* needle, size_t needleSize, uint8_t wildcard);

    size_t(*mGetInstructionSize)(TracerContext* ctx, const uint8_t* instruction);

    const uint8_t*(*mSearchSequence)(TracerContext* ctx, const uint8_t* start, const uint8_t* end, const uint8_t* seq, const char* mask);

    const uint8_t*(*mFindFunctionStart)(TracerContext* ctx, const uint8_t* offset, size_t size);
} TracerMemoryContext;

TracerContext* tracerCreateMemoryContext(int type, int size, int pid);

void tracerCleanupMemoryContext(TracerContext* ctx);

size_t tracerMemoryWrite(TracerContext* ctx, void* address, const void* buffer, size_t size);

size_t tracerMemoryRead(TracerContext* ctx, const void* address, void* buffer, size_t size);

void* tracerMemoryAlloc(TracerContext* ctx, size_t size);

void tracerMemoryFree(TracerContext* ctx, void* address);

TracerHandle tracerMemoryFindModule(TracerContext* ctx, const tchar* dllName);

const uint8_t* tracerMemorySearchPattern(TracerContext* ctx, const uint8_t* haystack, size_t haystackSize, const uint8_t* needle, size_t needleSize, uint8_t wildcard);

size_t tracerMemoryGetInstructionSize(TracerContext* ctx, const uint8_t* instruction);

const uint8_t* tracerMemorySearchSequence(TracerContext* ctx, const uint8_t* start, const uint8_t* end, const uint8_t* seq, const char* mask);

const uint8_t* tracerMemoryFindFunctionStart(TracerContext* ctx, const uint8_t* offset, size_t size);

#endif
