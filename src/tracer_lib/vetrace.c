
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

static TracerBool tracerVeTraceSetFlagsOnThread(DWORD threadId, TracerBool enable);

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

static TracerBool tracerVeTraceStart(TracerContext* ctx, void* address, int threadId) {

    // Set a breakpoint on the start address of the trace
    TracerHandle breakpoint = tracerSetHwBreakpointGlobal(tracerVeTraceStop, 1, eTracerBpCondExecute);

    return eTracerTrue;
}

static TracerBool tracerVeTraceStop(TracerContext* ctx, void* address, int threadId) {
    __asm nop
    __asm nop
    __asm nop
    __asm nop
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

static TracerBool tracerVeTraceSetFlagsOnThread(DWORD threadId, TracerBool enable) {
    DWORD accessFlags = THREAD_SUSPEND_RESUME
        | THREAD_GET_CONTEXT
        | THREAD_SET_CONTEXT
        | THREAD_QUERY_INFORMATION;

    TracerBool success = eTracerFalse;

    HANDLE thread = OpenThread(accessFlags, FALSE, threadId);
    if (thread) {
        if (SuspendThread(thread) != (DWORD)-1) {
            
            CONTEXT context;

            if (GetThreadContext(thread, &context)) {
                tracerVeTraceSetFlags(&context, enable);

                if (SetThreadContext(thread, &context)) {
                    success = eTracerTrue;
                } else {
                    tracerCoreSetLastError(eTracerErrorSystemCall);
                }
            } else {
                tracerCoreSetLastError(eTracerErrorSystemCall);
            }

            ResumeThread(thread);
        } else {
            tracerCoreSetLastError(eTracerErrorSystemCall);
        }

        CloseHandle(thread);
    } else {
        tracerCoreSetLastError(eTracerErrorSystemCall);
    }

    return success;
}



static LONG CALLBACK tracerVeTraceHandler(PEXCEPTION_POINTERS ex) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)tracerGetLocalProcessContext();
    TracerVeTraceContext* veTrace = (TracerVeTraceContext*)process->mTraceContext;

    switch (ex->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_SINGLE_STEP:
    {
        // Check if there is already an ongoing trace
        int index = tracerCoreGetActiveHwBreakpointIndex();

        if (index != -1) {

            // Make sure the enabled bit for the active breakpoint is turned on
            // This will restore the bit that we removed during the first call to the handler
            tracerHwBreakpointSetBits(&ex->ContextRecord->Dr7, index*2, 1, 1);

            // Enable branch tracing on this thread
            tracerVeTraceSetFlags(ex->ContextRecord, eTracerTrue);

        } else {
            // There was no active trace, check if the interrupt came from a hardware breakpoint

            // If it was triggered by a HW breakpoint we need to unset the control bit for this
            // index and restore it on the next call to the interrupt handler, otherwise
            // the interrupt handler will get called in an endless loop.
            uintptr_t exceptionAddr = (uintptr_t)ex->ExceptionRecord->ExceptionAddress;

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
            if (index == -1 || !tracerHwBreakpointGetBits(ex->ContextRecord->Dr7, index*2, 1)) {

                // The interrupt was not triggered by our tracer
                return EXCEPTION_CONTINUE_SEARCH;
            }

            // Temporarily remove the enabled bit for this breakpoint
            // Otherwise we would run into an endless loop, because the interrupt would
            // continue to be called on the breakpoint address.
            tracerHwBreakpointSetBits(&ex->ContextRecord->Dr7, index*2, 1, 0);

            // Save the index to restore the breakpoint on the next call to the handler
            tracerCoreSetActiveHwBreakpointIndex(index);

            // Enable branch tracing on this thread
            tracerVeTraceSetFlags(ex->ContextRecord, eTracerTrue);
        }

        uint64_t branchFrom = ex->ExceptionRecord->ExceptionInformation[0];
        uint64_t branchTo = ex->ContextRecord->Eip;

        if (branchFrom && branchTo) {
            uint8_t branchFromInsn = *(uint8_t*)branchFrom;
            uint8_t branchToInsn = *(uint8_t*)branchTo;

            char buffer[256];
            sprintf(buffer, "Branch from 0x%08llX (%02X) to 0x%08llX (%02X)\r\n",
                branchFrom, branchFromInsn, branchTo, branchToInsn);

            MessageBoxA(NULL, buffer, "Branch", MB_OK);
        }

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}