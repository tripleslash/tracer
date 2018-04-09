
#include <tracer_lib/process.h>
#include <tracer_lib/rwqueue.h>
#include <tracer_lib/memory.h>
#include <tracer_lib/symbol_resolver.h>

#include <assert.h>

#pragma comment(lib, "Zydis/Zydis.lib")

TracerContext* tracerCreateProcessContext(int type, int size, int pid) {
    assert(size >= sizeof(TracerProcessContext));

    TracerContext* ctx = tracerCoreCreateContext(type | eTracerProcessContext, size);
    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupProcessContext;

    TracerProcessContext* process = (TracerProcessContext*)ctx;
    process->mProcessId = pid;

    if (ZydisDecoderInit(&process->mDecoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32) != ZYDIS_STATUS_SUCCESS) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }
    if (ZydisFormatterInit(&process->mFormatter, ZYDIS_FORMATTER_STYLE_INTEL) != ZYDIS_STATUS_SUCCESS) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    tracerRegisterCustomSymbolResolver(&process->mFormatter);
    return ctx;
}

void tracerCleanupProcessContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return;
    }

    TracerProcessContext* process = (TracerProcessContext*)ctx;

    if (process->mSharedRWQueue) {
        tracerDestroyRWQueue(process->mSharedRWQueue);
        process->mSharedRWQueue = NULL;
    }

    if (process->mMappedView) {
        UnmapViewOfFile(process->mMappedView);
        process->mMappedView = NULL;
    }

    if (process->mSharedMemoryHandle) {
        CloseHandle(process->mSharedMemoryHandle);
        process->mSharedMemoryHandle = NULL;
    }

    if (process->mMemoryContext) {
        tracerCoreDestroyContext(process->mMemoryContext);
        process->mMemoryContext = NULL;
    }

    tracerCoreCleanupContext(ctx);
}

int tracerProcessGetPid(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return 0;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    return process->mProcessId;
}

TracerContext* tracerProcessGetMemoryContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return NULL;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    return process->mMemoryContext;
}

TracerBool tracerProcessStartTrace(TracerContext* ctx, const TracerStartTrace* startTrace) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return eTracerFalse;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    TLIB_METHOD_CHECK_SUPPORT(process->mStartTrace, eTracerFalse);
    return process->mStartTrace(ctx, startTrace);
}

TracerBool tracerProcessStopTrace(TracerContext* ctx, const TracerStopTrace* stopTrace) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return eTracerFalse;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    TLIB_METHOD_CHECK_SUPPORT(process->mStopTrace, eTracerFalse);
    return process->mStopTrace(ctx, stopTrace);
}

size_t tracerProcessFetchTraces(TracerContext* ctx, TracerTracedInstruction* outTraces, size_t maxElements) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return 0;
    }
    TracerProcessContext* process = (TracerProcessContext*)ctx;
    return tracerRWQueuePopAll(process->mSharedRWQueue, outTraces, maxElements);
}

TracerBool tracerProcessFormatInstruction(TracerContext* ctx, uintptr_t address, char* outBuffer, size_t bufferLength) {
    if (!tracerCoreValidateContext(ctx, eTracerProcessContext)) {
        return eTracerFalse;
    }

    TracerProcessContext* process = (TracerProcessContext*)ctx;

    uint8_t instBuffer[ZYDIS_MAX_INSTRUCTION_LENGTH];

    if (tracerMemoryRead(process->mMemoryContext,
            (const void*)address,
            instBuffer,
            sizeof(instBuffer)) != sizeof(instBuffer)) {

        return eTracerFalse;
    }

    ZydisDecodedInstruction decodedInst;

    if (ZydisDecoderDecodeBuffer(&process->mDecoder,
            instBuffer,
            sizeof(instBuffer),
            address,
            &decodedInst) != ZYDIS_STATUS_SUCCESS) {

        return eTracerFalse;
    }

    if (ZydisFormatterFormatInstruction(&process->mFormatter,
            &decodedInst,
            outBuffer,
            bufferLength) != ZYDIS_STATUS_SUCCESS) {

        return eTracerFalse;
    }

    return eTracerTrue;
}
