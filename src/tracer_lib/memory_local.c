
#include <tracer_lib/memory_local.h>

#include <assert.h>
#include <stdlib.h>

static size_t tracerMemoryLocalWrite(TracerContext* ctx, void* address, const void* buffer, size_t size);

static size_t tracerMemoryLocalRead(TracerContext* ctx, const void* address, void* buffer, size_t size);

static void* tracerMemoryLocalAlloc(TracerContext* ctx, size_t size);

static void tracerMemoryLocalFree(TracerContext* ctx, void* address);

static TracerHandle tracerMemoryLocalFindModule(TracerContext* ctx, const tchar* dllName);

static const uint8_t* tracerMemoryLocalSearchPattern(TracerContext* ctx, const uint8_t* haystack, size_t haystackSize, const uint8_t* needle, size_t needleSize, uint8_t wildcard);

static size_t tracerMemoryLocalGetInstructionSize(TracerContext* ctx, const uint8_t* instruction);

static const uint8_t* tracerMemoryLocalSearchSequence(TracerContext* ctx, const uint8_t* start, const uint8_t* end, const uint8_t* seq, const char* mask);

static const uint8_t* tracerMemoryLocalFindFunctionStart(TracerContext* ctx, const uint8_t* offset, size_t size);

static const uint8_t* tracerMemoryLocalFindFunctionStartEx(TracerContext* ctx, const uint8_t* offset, size_t size, const uint8_t* prologue, const char* mask);


TracerContext* tracerCreateLocalMemoryContext(int type, int size) {
    assert(size >= sizeof(TracerLocalMemoryContext));

    int pid = (int)GetCurrentProcessId();

    TracerContext* ctx = tracerCreateMemoryContext(
        type | eTracerMemoryContextLocal, size, pid);

    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupLocalMemoryContext;

    TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
    memory->mWriteMemory = tracerMemoryLocalWrite;
    memory->mReadMemory = tracerMemoryLocalRead;
    memory->mAllocMemory = tracerMemoryLocalAlloc;
    memory->mFreeMemory = tracerMemoryLocalFree;
    memory->mFindModule = tracerMemoryLocalFindModule;
    memory->mSearchPattern = tracerMemoryLocalSearchPattern;
    memory->mGetInstructionSize = tracerMemoryLocalGetInstructionSize;
    memory->mSearchSequence = tracerMemoryLocalSearchSequence;
    memory->mFindFunctionStart = tracerMemoryLocalFindFunctionStart;

    memory->mModuleHandle = tracerCoreGetModuleHandle();
    memory->mProcessHandle = (TracerHandle)GetCurrentProcess();

    return ctx;
}

void tracerCleanupLocalMemoryContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerMemoryContextLocal)) {
        return;
    }
    tracerCleanupMemoryContext(ctx);
}

static size_t tracerMemoryLocalWrite(TracerContext* ctx, void* address, const void* buffer, size_t size) {
    memcpy(address, buffer, size);
    return size;
}

static size_t tracerMemoryLocalRead(TracerContext* ctx, const void* address, void* buffer, size_t size) {
    memcpy(buffer, address, size);
    return size;
}

static void* tracerMemoryLocalAlloc(TracerContext* ctx, size_t size) {
    void* buffer = calloc(1, size);
    if (!buffer) {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
    }
    return buffer;
}

static void tracerMemoryLocalFree(TracerContext* ctx, void* address) {
    free(address);
}

static TracerHandle tracerMemoryLocalFindModule(TracerContext* ctx, const tchar* dllName) {
    if (!dllName) {
        TracerMemoryContext* memory = (TracerMemoryContext*)ctx;
        return memory->mModuleHandle;
    }

    return (TracerHandle)GetModuleHandle(dllName);
}

static void tracerFillShiftTableBoyerMoore(const uint8_t* pattern,
    size_t size, uint8_t wildcard, size_t* badCharSkip) {

    size_t idx = 0;
    size_t last = size - 1;

    // Get last wildcard position
    for (idx = last; idx > 0 && pattern[idx] != wildcard; --idx);

    size_t diff = last - idx;

    if (diff == 0) {
        diff = 1;
    }

    // Prepare shift table
    for (idx = 0; idx < 256; ++idx) {
        badCharSkip[idx] = diff;
    }

    for (idx = last - diff; idx < last; ++idx) {
        badCharSkip[pattern[idx]] = last - idx;
    }
}

static const uint8_t* tracerMemoryLocalSearchPattern(TracerContext* ctx, const uint8_t* haystack,
    size_t haystackSize, const uint8_t* needle, size_t needleSize, uint8_t wildcard) {

    size_t badCharSkip[256];
    tracerFillShiftTableBoyerMoore(needle, needleSize, wildcard, badCharSkip);

    intptr_t last = ((intptr_t)needleSize) - 1;
    const uint8_t* scanEnd = haystack + haystackSize - needleSize;

    for (; haystack <= scanEnd; haystack += badCharSkip[haystack[last]]) {
        for (intptr_t idx = last; idx >= 0; --idx) {
            if (needle[idx] != wildcard && haystack[idx] != needle[idx]) {
                goto skip;
            }
            else if (idx == 0) {
                return haystack;
            }
        }
    skip:;
    }
    return NULL;
}

#pragma code_seg(push, ".text")
__declspec(allocate(".text")) const static uint8_t MLDE32[] = {
    0x60, 0xFC, 0x33, 0xD2, 0x8B, 0x74, 0x24, 0x24,
    0x8B, 0xEC, 0x68, 0x1C, 0xF7, 0x97, 0x10, 0x68,
    0x80, 0x67, 0x1C, 0xF7, 0x68, 0x18, 0x97, 0x38,
    0x17, 0x68, 0x18, 0xB7, 0x1C, 0x10, 0x68, 0x17,
    0x2C, 0x30, 0x17, 0x68, 0x17, 0x30, 0x17, 0x18,
    0x68, 0x47, 0xF5, 0x15, 0xF7, 0x68, 0x48, 0x37,
    0x10, 0x4C, 0x68, 0xF7, 0xE7, 0x2C, 0x27, 0x68,
    0x87, 0x60, 0xAC, 0xF7, 0x68, 0x52, 0x1C, 0x12,
    0x1C, 0x68, 0x1C, 0x87, 0x10, 0x7C, 0x68, 0x1C,
    0x70, 0x1C, 0x20, 0x68, 0x2B, 0x60, 0x67, 0x47,
    0x68, 0x11, 0x10, 0x21, 0x20, 0x68, 0x25, 0x16,
    0x12, 0x40, 0x68, 0x22, 0x20, 0x87, 0x82, 0x68,
    0x20, 0x12, 0x20, 0x47, 0x68, 0x19, 0x14, 0x10,
    0x13, 0x68, 0x13, 0x10, 0x27, 0x18, 0x68, 0x60,
    0x82, 0x85, 0x28, 0x68, 0x45, 0x40, 0x12, 0x15,
    0x68, 0xC7, 0xA0, 0x16, 0x50, 0x68, 0x12, 0x18,
    0x19, 0x28, 0x68, 0x12, 0x18, 0x40, 0xF2, 0x68,
    0x27, 0x41, 0x15, 0x19, 0x68, 0x11, 0xF0, 0xF0,
    0x50, 0xB9, 0x10, 0x47, 0x12, 0x15, 0x51, 0x68,
    0x47, 0x12, 0x15, 0x11, 0x68, 0x12, 0x15, 0x11,
    0x10, 0x68, 0x15, 0x11, 0x10, 0x47, 0xB8, 0x15,
    0x20, 0x47, 0x12, 0x50, 0x50, 0x68, 0x10, 0x1A,
    0x47, 0x12, 0x80, 0xC1, 0x10, 0x51, 0x80, 0xE9,
    0x20, 0x51, 0x33, 0xC9, 0x49, 0x41, 0x8B, 0xFC,
    0xAC, 0x8A, 0xF8, 0x8A, 0x27, 0x47, 0xC0, 0xEC,
    0x04, 0x2A, 0xC4, 0x73, 0xF6, 0x8A, 0x47, 0xFF,
    0x24, 0x0F, 0x3C, 0x0C, 0x75, 0x03, 0x5A, 0xF7,
    0xD2, 0x42, 0x3C, 0x00, 0x74, 0x42, 0x3C, 0x01,
    0x74, 0xDB, 0x83, 0xC7, 0x51, 0x3C, 0x0A, 0x74,
    0xD7, 0x8B, 0x7D, 0x24, 0x42, 0x3C, 0x02, 0x74,
    0x2F, 0x3C, 0x07, 0x74, 0x33, 0x3C, 0x0B, 0x0F,
    0x84, 0x7E, 0x00, 0x00, 0x00, 0x42, 0x3C, 0x03,
    0x74, 0x1E, 0x3C, 0x08, 0x74, 0x22, 0x42, 0x3C,
    0x04, 0x74, 0x15, 0x42, 0x42, 0x60, 0xB0, 0x66,
    0xF2, 0xAE, 0x61, 0x75, 0x02, 0x4A, 0x4A, 0x3C,
    0x09, 0x74, 0x0D, 0x2C, 0x05, 0x74, 0x6C, 0x42,
    0x8B, 0xE5, 0x89, 0x54, 0x24, 0x1C, 0x61, 0xC3,
    0xAC, 0x8A, 0xE0, 0xC0, 0xE8, 0x07, 0x72, 0x12,
    0x74, 0x14, 0x80, 0xC2, 0x04, 0x60, 0xB0, 0x67,
    0xF2, 0xAE, 0x61, 0x75, 0x09, 0x80, 0xEA, 0x03,
    0xFE, 0xC8, 0x75, 0xDC, 0x42, 0x40, 0x80, 0xE4,
    0x07, 0x60, 0xB0, 0x67, 0xF2, 0xAE, 0x61, 0x74,
    0x13, 0x80, 0xFC, 0x04, 0x74, 0x17, 0x80, 0xFC,
    0x05, 0x75, 0xC5, 0xFE, 0xC8, 0x74, 0xC1, 0x80,
    0xC2, 0x04, 0xEB, 0xBC, 0x66, 0x3D, 0x00, 0x06,
    0x75, 0xB6, 0x42, 0xEB, 0xB2, 0x3C, 0x00, 0x75,
    0xAE, 0xAC, 0x24, 0x07, 0x2C, 0x05, 0x75, 0xA7,
    0x42, 0xEB, 0xE4, 0xF6, 0x06, 0x38, 0x75, 0xA8,
    0xB0, 0x08, 0xD0, 0xEF, 0x14, 0x00, 0xE9, 0x72,
    0xFF, 0xFF, 0xFF, 0x80, 0xEF, 0xA0, 0x80, 0xFF,
    0x04, 0x73, 0x82, 0x60, 0xB0, 0x67, 0xF2, 0xAE,
    0x61, 0x75, 0x02, 0x4A, 0x4A, 0x60, 0xB0, 0x66,
    0xF2, 0xAE, 0x61, 0x0F, 0x84, 0x76, 0xFF, 0xFF,
    0xFF, 0x0F, 0x85, 0x66, 0xFF, 0xFF, 0xFF
};
#pragma code_seg(pop)

static size_t tracerMemoryLocalGetInstructionSize(TracerContext* ctx, const uint8_t* instruction) {
    return ((size_t(__cdecl*)(const uint8_t*))&MLDE32[0])(instruction);
}

static TracerBool tracerMemoryLocalCompare(const uint8_t* data, const uint8_t* pattern, const char* mask) {
    for (; *mask; ++mask, ++data, ++pattern) {
        if (*mask == 'x' && *data != *pattern) {
            return eTracerFalse;
        }
    }
    return eTracerTrue;
}

static const uint8_t* tracerMemoryLocalSearchSequence(TracerContext* ctx, const uint8_t* start,
    const uint8_t* end, const uint8_t* seq, const char* mask) {

    while (start != end) {
        if (tracerMemoryLocalCompare(start, seq, mask)) {
            return start;
        }
        start += (start < end) ? +1 : -1;
    }
    return NULL;
}

static const uint8_t* tracerMemoryLocalFindFunctionStart(TracerContext* ctx, const uint8_t* offset, size_t size) {
    typedef struct TracerPrologue {
        const uint8_t* bytes;
        const char* mask;
    } TracerPrologue;

    static const TracerPrologue prologues[] = {
        { "\x55\x8B\xEC", "xxx" },
        { "\x56\x8B\xF1", "xxx" },
    };

    const uint8_t* start = NULL;

    for (int i = 0; i < _countof(prologues); ++i) {
        const uint8_t* current = tracerMemoryLocalFindFunctionStartEx(
            ctx, offset, size, prologues[i].bytes, prologues[i].mask);

        const uint8_t* furthest = min(start, current);
        const uint8_t* nearest = max(start, current);

        // Needs to handle double prologues e.g.

        // push ebp
        // mov ebp, esp
        // push esi
        // mov esi, ecx

        static const int doublePrologueThreshold = 0x10;

        if (furthest + doublePrologueThreshold > nearest) {
            start = furthest;
        }
        if (nearest - doublePrologueThreshold > furthest) {
            start = nearest;
        }
    }
    return start;
}

static const uint8_t* tracerMemoryLocalFindFunctionStartEx(TracerContext* ctx, const uint8_t* offset,
    size_t size, const uint8_t* prologue, const char* mask) {

    const uint8_t* start = offset;
    const uint8_t* end = offset - size;

    for (; end < start; --start) {
        start = tracerMemoryLocalSearchSequence(ctx, start, end, prologue, mask);

        // Prologue not found - abort
        if (!start) {
            return NULL;
        }

        // Try to disassemble downwards from the prologue to see if we can end up at the original offset
        const uint8_t* tmp = start;

        while (tmp < offset) {
            size_t inst = tracerMemoryLocalGetInstructionSize(ctx, tmp);
            if (!inst) {
                break;
            }
            tmp += inst;
        }

        if (tmp == offset) {
            return start;
        }
    }
    return NULL;
}
