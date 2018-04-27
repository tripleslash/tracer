
#include <tracer_lib/process_local.h>
#include <tracer_lib/memory_local.h>
#include <tracer_lib/symbol_resolver.h>
#include <tracer_lib/vetrace.h>
#include <tracer_lib/rwqueue.h>

#include <assert.h>

#pragma comment(lib, "Zydis/Zydis.lib")

static TracerBool tracerProcessLocalInit(TracerContext* ctx);

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx);

static TracerBool tracerProcessLocalStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace);

static TracerBool tracerProcessLocalStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace);

static const char* tracerProcessLocalDecodeAndFormatInstruction(TracerContext* ctx, TracerDecodeAndFormat* decodeAndFmt);

static TracerContext* gTracerLocalProcessContext = NULL;

TracerContext* tracerCreateLocalProcessContext(int type, int size, TracerHandle sharedMemoryHandle) {
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
    process->mSharedMemoryHandle = sharedMemoryHandle;
    process->mStartTrace = tracerProcessLocalStartTrace;
    process->mStopTrace = tracerProcessLocalStopTrace;
    process->mDecodeAndFormat = tracerProcessLocalDecodeAndFormatInstruction;

    process->mMemoryContext = tracerCreateLocalMemoryContext(
        eTracerMemoryContextLocal, sizeof(TracerLocalMemoryContext));

    if (!process->mMemoryContext) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    if (process->mSharedMemoryHandle) {
        process->mMappedView = MapViewOfFile(process->mSharedMemoryHandle,
            FILE_MAP_ALL_ACCESS, 0, 0, TLIB_SHARED_MEMORY_SIZE);

        if (!process->mMappedView) {
            tracerCoreDestroyContext(ctx);
            return NULL;
        }
    }

    process->mSharedRWQueue = tracerCreateRWQueue(process->mMappedView,
        TLIB_SHARED_MEMORY_SIZE, sizeof(TracerTracedInstruction));

    if (!process->mSharedRWQueue) {
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
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    TracerLocalProcessContext* local = (TracerLocalProcessContext*)ctx;

    local->mTraceContext = tracerCreateVeTraceContext(eTracerTraceContextVEH,
        sizeof(TracerVeTraceContext), process->mSharedRWQueue);

    if (!local->mTraceContext) {
        return eTracerFalse;
    }

    if (ZydisDecoderInit(&local->mDecoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32) != ZYDIS_STATUS_SUCCESS) {
        return eTracerFalse;
    }
    if (ZydisFormatterInit(&local->mFormatter, ZYDIS_FORMATTER_STYLE_INTEL) != ZYDIS_STATUS_SUCCESS) {
        return eTracerFalse;
    }

    tracerRegisterCustomSymbolResolver(&local->mFormatter);
    return eTracerTrue;
}

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    if (process->mTraceContext) {
        tracerCoreDestroyContext(process->mTraceContext);
        process->mTraceContext = NULL;
    }

    return eTracerTrue;
}

static TracerBool tracerProcessLocalStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    return tracerTraceStart(process->mTraceContext,
        startTrace->mAddress,
        startTrace->mThreadId,
        startTrace->mMaxTraceDepth,
        startTrace->mLifetime);
}

static TracerBool tracerProcessLocalStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;
    return tracerTraceStop(process->mTraceContext, stopTrace->mAddress, stopTrace->mThreadId);
}

static const char* tracerProcessLocalDecodeAndFormatInstruction(TracerContext* ctx, TracerDecodeAndFormat* decodeAndFmt) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    ZydisDecodedInstruction decodedInst;

    if (ZydisDecoderDecodeBuffer(&process->mDecoder,
            (const void*)decodeAndFmt->mAddress,
            ZYDIS_MAX_INSTRUCTION_LENGTH,
            decodeAndFmt->mAddress,
            &decodedInst) != ZYDIS_STATUS_SUCCESS) {

        tracerCoreSetLastError(eTracerErrorSystemCall);
        return NULL;
    }

    if (ZydisFormatterFormatInstruction(&process->mFormatter,
            &decodedInst,
            decodeAndFmt->mOutBuffer,
            decodeAndFmt->mBufferLength) != ZYDIS_STATUS_SUCCESS) {

        tracerCoreSetLastError(eTracerErrorSystemCall);
        return NULL;
    }

    return decodeAndFmt->mOutBuffer;
}
