
#include <tracer_lib/vetrace.h>
#include <tracer_lib/hwbp.h>
#include <tracer_lib/process_local.h>

#include <stdio.h>
#include <assert.h>

static TracerBool tracerVeTraceInit(TracerContext* ctx);

static TracerBool tracerVeTraceShutdown(TracerContext* ctx);

static TracerBool tracerVeTraceStart(TracerContext* ctx, void* address, int threadId);

static TracerBool tracerVeTraceStop(TracerContext* ctx, void* address, int threadId);

static void tracerVeTraceSetFlags(PCONTEXT context, TracerBool enable);

static LONG CALLBACK tracerVeTraceHandler(PEXCEPTION_POINTERS ex);

TracerContext* tracerCreateVeTraceContext(int type, int size) {
    assert(size >= sizeof(TracerVeTraceContext));

    TracerContext* ctx = tracerCoreCreateContext(type | eTracerTraceContextVEH, size);
    if (!ctx) {
        return NULL;
    }

    TracerBaseContext* base = (TracerBaseContext*)ctx;
    base->mCleanup = tracerCleanupVeTraceContext;

    TracerTraceContext* trace = (TracerTraceContext*)ctx;
    trace->mStartTrace = tracerVeTraceStart;
    trace->mStopTrace = tracerVeTraceStop;

    if (!tracerVeTraceInit(ctx)) {
        tracerCoreDestroyContext(ctx);
        return NULL;
    }

    return ctx;
}

void tracerCleanupVeTraceContext(TracerContext* ctx) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContextVEH)) {
        return;
    }
    tracerVeTraceShutdown(ctx);
    tracerCleanupTraceContext(ctx);
}

static TracerBool tracerVeTraceInit(TracerContext* ctx) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    // Initialize the Zydis library
    ZydisDecoderInit(&trace->mDecoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32);

    uintptr_t moduleBase = (uintptr_t)tracerCoreGetModuleHandle();

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)moduleBase;
    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)(moduleBase + dosHeader->e_lfanew);

    trace->mBaseOfCode = moduleBase + ntHeader->OptionalHeader.BaseOfCode;
    trace->mSizeOfCode = ntHeader->OptionalHeader.SizeOfCode;

    // Register the VEH. To avoid unwanted calls, make sure it is the last handler in the chain.
    trace->mAddVehHandle = AddVectoredExceptionHandler(TRUE, tracerVeTraceHandler);

    if (!trace->mAddVehHandle) {
        return eTracerFalse;
    }

    return eTracerTrue;
}

static TracerBool tracerVeTraceShutdown(TracerContext* ctx) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    // Remove the VEH. We also need to make sure no thread still has trace flags set.
    RemoveVectoredExceptionHandler(trace->mAddVehHandle);
    trace->mAddVehHandle = NULL;

    return eTracerTrue;
}

static __declspec(noinline) void tracerTestTrace(void) {
    puts("hello world");
}

static TracerBool tracerVeTraceStart(TracerContext* ctx, void* address, int threadId) {

    // Set a breakpoint on the start address of the trace
    TracerHandle breakpoint = tracerSetHwBreakpointGlobal(tracerTestTrace, 1, eTracerBpCondExecute);

    char buf[256];
    sprintf(buf, "Started tracing at %p", tracerTestTrace);
    MessageBoxA(0, buf, "", MB_OK);

    tracerTestTrace();
    return eTracerTrue;
}

static TracerBool tracerVeTraceStop(TracerContext* ctx, void* address, int threadId) {
    MessageBoxA(0, "Stopped tracing", "", MB_OK);
    return eTracerFalse;
}

#define TLIB_VETRACE_DR7_LBR                 0x100       // Last Branch Record (Bit 8 in DR7)
                                                         // Mapped to Model Specific Register in Kernel (MSR).

#define TLIB_VETRACE_DR7_BTF                 0x200       // Branch Trap Flag (Bit 9 in DR7)
                                                         // Enables trap on next branch
                                                         // Mapped to Model Specific Register in Kernel (MSR).

#define TLIB_VETRACE_EFLAGS_SINGLE_STEP      0x100       // Single Step Flag (Trap on next instruction)

static void tracerVeTraceSetFlags(PCONTEXT context, TracerBool enable) {
    // Enable or disable the trace flags for the given thread context

    if (enable) {
        context->Dr7 |= (TLIB_VETRACE_DR7_LBR | TLIB_VETRACE_DR7_BTF);
        context->EFlags |= TLIB_VETRACE_EFLAGS_SINGLE_STEP;
    } else {
        context->Dr7 &= ~(TLIB_VETRACE_DR7_LBR | TLIB_VETRACE_DR7_BTF);
        context->EFlags &= ~TLIB_VETRACE_EFLAGS_SINGLE_STEP;
    }
}

static TracerBool tracerVeTraceIsAddressInExcludedModule(TracerContext* ctx, uintptr_t address) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;
    return address < trace->mBaseOfCode || address >= trace->mBaseOfCode + trace->mSizeOfCode;
}

static TracerBool tracerVeHasSuspendedTrace() {
    return tracerCoreGetSuspendedHwBreakpointIndex() != -1;
}

static void tracerVeSuspendTrace(PCONTEXT ctx) {
    char buf[256];
    sprintf(buf, "Suspended at %p resume point %p", ctx->Eip, *(void**)ctx->Esp);
    MessageBoxA(0, buf, "", MB_OK);

    int suspendIndex = tracerSetHwBreakpointOnContext(
        *(void**)ctx->Esp, 1, ctx, eTracerBpCondExecute);

    tracerCoreSetSuspendedHwBreakpointIndex(suspendIndex);

    // Disable branch tracing for now
    tracerVeTraceSetFlags(ctx, eTracerFalse);
}

static void tracerVeResumeTrace(PCONTEXT ctx) {
    // Disable the breakpoint that we used to resume the trace
    int suspendIndex = tracerCoreGetSuspendedHwBreakpointIndex();
    tracerHwBreakpointSetBits(&ctx->Dr7, suspendIndex << 1, 1, 0);

    // Reenable branch tracing
    tracerVeTraceSetFlags(ctx, eTracerTrue);

    // Unset suspended index
    tracerCoreSetSuspendedHwBreakpointIndex(-1);
}

static TracerTracedInstruction* tracerVeAddTrace(TracerContext* ctx, const void* address) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    if (trace->mTraceLength >= TLIB_MAX_TRACE_LENGTH) {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
        return NULL;
    }

    TracerTracedInstruction* inst = &trace->mTraceArray[trace->mTraceLength++];
    ZydisDecoderDecodeBuffer(&trace->mDecoder, address, ZYDIS_MAX_INSTRUCTION_LENGTH, 0, &inst->mInstruction);

    inst->mTraceId = trace->mCurrentTraceId;

    switch (inst->mInstruction.mnemonic) {
    case ZYDIS_MNEMONIC_CALL:
        inst->mType = eTracerNodeTypeCall;
        break;
    case ZYDIS_MNEMONIC_RET:
    case ZYDIS_MNEMONIC_IRET:
    case ZYDIS_MNEMONIC_IRETD:
    case ZYDIS_MNEMONIC_IRETQ:
        inst->mType = eTracerNodeTypeReturn;
        break;
    default:
        inst->mType = eTracerNodeTypeBranch;
    }

    return inst;
}

static TracerBool tracerVeTraceInstruction(TracerContext* ctx, PEXCEPTION_POINTERS ex, TracerBool isNewTrace) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    uint8_t* branchDst = (uint8_t*)ex->ExceptionRecord->ExceptionAddress;
    uint8_t* branchSrc = (uint8_t*)ex->ExceptionRecord->ExceptionInformation[0];

    if (isNewTrace) {
        branchSrc = branchDst;

        trace->mCallDepth = 0;
        trace->mCurrentTraceId++;
    }

    if (!branchSrc) {
        MessageBoxA(0, "no branch src", "", MB_OK);
        return eTracerFalse;
    }

    TracerTracedInstruction* inst = tracerVeAddTrace(ctx, branchSrc);
    if (inst == NULL) {
        MessageBoxA(0, "out of memory", "", MB_OK);
        return eTracerFalse;
    }

    char b[256];

    switch (inst->mType) {
    case eTracerNodeTypeCall:
        trace->mCallDepth++;
        sprintf(b, "call at %p", branchSrc);
        MessageBoxA(0, b, "", MB_OK);
        break;
    case eTracerNodeTypeReturn:
        trace->mCallDepth--;
        sprintf(b, "return at %p", branchSrc);
        MessageBoxA(0, b, "", MB_OK);
        break;
    }

    if (trace->mCallDepth < 0) {
        char buf[256];
        sprintf(buf, "Stopped tracing at %p", branchSrc);
        MessageBoxA(0, buf, "", MB_OK);

        return eTracerFalse;
    }

    return eTracerTrue;
}

static LONG CALLBACK tracerVeTraceHandler(PEXCEPTION_POINTERS ex) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)tracerGetLocalProcessContext();

    uintptr_t exceptionAddr = (uintptr_t)ex->ExceptionRecord->ExceptionAddress;

    switch (ex->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_SINGLE_STEP:
    {
        TracerBool isNewTrace = eTracerFalse;

        // Check if there is already an ongoing trace
        int index = tracerCoreGetActiveHwBreakpointIndex();

        if (index == -1) {
            // There was no active trace, check if the interrupt came from a hardware breakpoint

            // If it was triggered by a HW breakpoint we need to unset the control bit for this
            // index and restore it on the next call to the interrupt handler, otherwise
            // the interrupt handler will get called in an endless loop.
            if (exceptionAddr == ex->ContextRecord->Dr0) {
                index = 0;
            } else if (exceptionAddr == ex->ContextRecord->Dr1) {
                index = 1;
            } else if (exceptionAddr == ex->ContextRecord->Dr2) {
                index = 2;
            } else if (exceptionAddr == ex->ContextRecord->Dr3) {
                index = 3;
            }

            // Check if this index was actually enabled in the debug control register
            if (index == -1 || !tracerHwBreakpointGetBits(ex->ContextRecord->Dr7, index << 1, 1)) {

                // The interrupt was not triggered by our tracer
                return EXCEPTION_CONTINUE_SEARCH;
            }

            // Temporarily remove the enabled bit for this breakpoint
            // We will restore it on the next call to the handler. The breakpoint index
            // will be stored in thread local storage for the time being.
            tracerHwBreakpointSetBits(&ex->ContextRecord->Dr7, index << 1, 1, 0);

            // This will back up the breakpoint index into the thread local storage
            tracerCoreSetActiveHwBreakpointIndex(index);

            isNewTrace = eTracerTrue;

        } else {

            // Restore the bit that we removed during the first call to the handler
            tracerHwBreakpointSetBits(&ex->ContextRecord->Dr7, index << 1, 1, 1);
        }

        if (tracerVeHasSuspendedTrace()) {
            // Check if we have to resume the tracing after a call to an excluded library function

            tracerVeResumeTrace(ex->ContextRecord);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        if (tracerVeTraceIsAddressInExcludedModule(process->mTraceContext, exceptionAddr)) {
            // We are not interested in tracing calls inside windows libraries.

            // We suspend tracing by temporarily adding a hardware breakpoint on the place  that the call
            // will return to. During this time we disable branch tracing completely.

            // Once the resume hardware breakpoint is triggered, the breakpoint is removed and the tracing
            // will continue.

            tracerVeSuspendTrace(ex->ContextRecord);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        // If this function returns false it means that the tracing for the current
        // thread should be disabled. In this case we remove the branch trace flags.
        if (tracerVeTraceInstruction(process->mTraceContext, ex, isNewTrace)) {

            // Keep branch tracing on this thread
            tracerVeTraceSetFlags(ex->ContextRecord, eTracerTrue);

        } else {

            // Disable branch tracing on this thread
            tracerVeTraceSetFlags(ex->ContextRecord, eTracerFalse);

            // And remove the stored tls index for the breakpoint
            tracerCoreSetActiveHwBreakpointIndex(-1);
        }

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}
