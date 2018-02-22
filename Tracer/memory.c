
#include "memory.h"
#include <Psapi.h>

#pragma comment(lib, "psapi")

#define TRACER_MEMORY_CHECK_SUPPORT(function, ...) \
    if (!memory->function) { \
        tracerCoreSetLastError(eTracerErrorNotImplemented); \
        return __VA_ARGS__; \
    }

TracerContext* tracerCreateMemoryContext(int type, int size, int pid) {
    assert(size >= sizeof(TracerMemoryContext));

    TracerContext* ctx = tracerCoreCreateContext(type | eTracerMemoryContext, size);
    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupMemoryContext;

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    memory->mProcessId = pid;

    return ctx;
}

void tracerCleanupMemoryContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    CloseHandle((HANDLE)memory->mProcessHandle);
    memory->mProcessHandle = NULL;

    tracerCoreCleanupContext(ctx);
}

size_t tracerMemoryWrite(TracerContext* ctx, void* address, const void* buffer, size_t size) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return 0;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mWriteMemory, 0);
    return memory->mWriteMemory(ctx, address, buffer, size);
}

size_t tracerMemoryRead(TracerContext* ctx, const void* address, void* buffer, size_t size) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return 0;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mReadMemory, 0);
    return memory->mReadMemory(ctx, address, buffer, size);
}

void* tracerMemoryAlloc(TracerContext* ctx, size_t size) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return NULL;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mAllocMemory, NULL);
    return memory->mAllocMemory(ctx, size);
}

void tracerMemoryFree(TracerContext* ctx, void* address) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mFreeMemory);
    memory->mFreeMemory(ctx, address);
}

TracerHandle tracerMemoryFindModule(TracerContext* ctx, const tchar* dllName) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return NULL;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mFindModule, NULL);
    return memory->mFindModule(ctx, dllName);
}

const uint8_t * tracerMemorySearchPattern(TracerContext* ctx, const uint8_t* haystack,
    size_t haystackSize, const uint8_t* needle, size_t needleSize, uint8_t wildcard) {

    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return NULL;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mSearchPattern, NULL);
    return memory->mSearchPattern(ctx, haystack, haystackSize, needle, needleSize, wildcard);
}

size_t tracerMemoryGetInstructionSize(TracerContext* ctx, const uint8_t* instruction) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return 0;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mGetInstructionSize, 0);
    return memory->mGetInstructionSize(ctx, instruction);
}

const uint8_t* tracerMemorySearchSequence(TracerContext* ctx, const uint8_t* start,
    const uint8_t* end, const uint8_t* seq, const char* mask) {

    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return NULL;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mSearchSequence, NULL);
    return memory->mSearchSequence(ctx, start, end, seq, mask);
}

const uint8_t* tracerMemoryFindFunctionStart(TracerContext* ctx, const uint8_t* offset,
    size_t size) {

    if (!tracerCoreValidateContext(ctx, eTracerMemoryContext)) {
        return NULL;
    }

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    TRACER_MEMORY_CHECK_SUPPORT(mFindFunctionStart, NULL);
    return memory->mFindFunctionStart(ctx, offset, size);
}
