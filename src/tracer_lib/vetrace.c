
#include <tracer_lib/vetrace.h>
#include <tracer_lib/hwbp.h>
#include <tracer_lib/process_local.h>
#include <tracer_lib/rwqueue.h>

#include <stdio.h>
#include <assert.h>

#include <TlHelp32.h>

#pragma comment(lib, "Zydis/Zydis.lib")

#define TLIB_VETRACE_DR7_LBR                 0x100       // Last Branch Record (Bit 8 in DR7)
                                                         // Mapped to Model Specific Register in Kernel (MSR).

#define TLIB_VETRACE_DR7_BTF                 0x200       // Branch Trap Flag (Bit 9 in DR7)
                                                         // Enables trap on next branch
                                                         // Mapped to Model Specific Register in Kernel (MSR).

#define TLIB_VETRACE_EFLAGS_SINGLE_STEP      0x100       // Single Step Flag (Trap on next instruction)

static TracerBool tracerVeTraceInit(TracerContext* ctx);

static TracerBool tracerVeTraceShutdown(TracerContext* ctx);

static TracerBool tracerVeTraceStart(TracerContext* ctx, void* address, int threadId);

static TracerBool tracerVeTraceStop(TracerContext* ctx, void* address, int threadId);

static void tracerVeTraceSetFlags(PCONTEXT context, TracerBool enable);

static LONG CALLBACK tracerVeTraceHandler(PEXCEPTION_POINTERS ex);

TracerContext* tracerCreateVeTraceContext(int type, int size, TracerHandle traceQueue) {
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

    TracerVeTraceContext* veTrace = (TracerVeTraceContext*)ctx;
    veTrace->mSharedRWQueue = traceQueue;

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

    if (ZydisDecoderInit(&trace->mDecoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32) != ZYDIS_STATUS_SUCCESS) {
        return eTracerFalse;
    }
    if (ZydisFormatterInit(&trace->mFormatter, ZYDIS_FORMATTER_STYLE_INTEL) != ZYDIS_STATUS_SUCCESS) {
        return eTracerFalse;
    }

    // Register the VEH. To avoid unwanted calls, make sure it is the last handler in the chain.
    trace->mAddVehHandle = AddVectoredExceptionHandler(TRUE, tracerVeTraceHandler);

    if (!trace->mAddVehHandle) {
        return eTracerFalse;
    }

    return eTracerTrue;
}

static TracerBool tracerVeTraceShutdown(TracerContext* ctx) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    if (trace->mAddVehHandle) {
        RemoveVectoredExceptionHandler(trace->mAddVehHandle);
        trace->mAddVehHandle = NULL;
    }

    return eTracerTrue;
}

static TracerBool tracerVeFindModuleBoundsForAddress(uintptr_t address, uintptr_t* outBaseOfCode, uintptr_t* outSizeOfCode) {
    uintptr_t moduleBaseOfCode = 0;
    uintptr_t moduleSizeOfCode = 0;

    DWORD pid = GetCurrentProcessId();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);

    if (snapshot != INVALID_HANDLE_VALUE) {

        MODULEENTRY32 entry;
        entry.dwSize = sizeof(entry);

        if (Module32First(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID != pid) {
                    // The snapshot contained a process ID that we aren't even looking for
                    continue;
                }
                TracerHandle module = (TracerHandle)entry.hModule;

                if (!module || module == INVALID_HANDLE_VALUE) {
                    module = (TracerHandle)entry.modBaseAddr;
                }

                PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)module;
                PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((uint8_t*)module + dosHeader->e_lfanew);

                uintptr_t baseOfCode = (uintptr_t)module + ntHeader->OptionalHeader.BaseOfCode;
                uintptr_t sizeOfCode = ntHeader->OptionalHeader.SizeOfCode;

                if (address >= baseOfCode && address < baseOfCode + sizeOfCode) {
                    moduleBaseOfCode = baseOfCode;
                    moduleSizeOfCode = sizeOfCode;

                    break;
                }

            } while (Module32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
    }

    if (!moduleBaseOfCode || !moduleSizeOfCode) {
        return eTracerFalse;
    }

    if (outBaseOfCode) {
        *outBaseOfCode = moduleBaseOfCode;
    }
    if (outSizeOfCode) {
        *outSizeOfCode = moduleSizeOfCode;
    }
    return eTracerTrue;
}

static TracerBool tracerVeTraceStart(TracerContext* ctx, void* address, int threadId) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContextVEH)) {
        return eTracerFalse;
    }

    uintptr_t baseOfCode = 0;
    uintptr_t sizeOfCode = 0;

    if (!tracerVeFindModuleBoundsForAddress((uintptr_t)address, &baseOfCode, &sizeOfCode)) {
        tracerCoreSetLastError(eTracerErrorSystemCall);
        return eTracerFalse;
    }

    TracerHandle breakpoint = NULL;

    if (threadId >= 0) {
        breakpoint = tracerSetHwBreakpointOnThread(address, 1, threadId, eTracerBpCondExecute);
    } else {
        breakpoint = tracerSetHwBreakpointGlobal(address, 1, eTracerBpCondExecute);
    }

    if (!breakpoint) {
        tracerCoreSetLastError(eTracerErrorOutOfResources);
        return eTracerFalse;
    }

    TracerActiveTrace* activeTrace = (TracerActiveTrace*)malloc(sizeof(TracerActiveTrace));
    if (!activeTrace) {
        tracerRemoveHwBreakpoint(breakpoint);

        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
        return eTracerFalse;
    }

    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    activeTrace->mStartAddress = address;
    activeTrace->mBaseOfCode = baseOfCode;
    activeTrace->mSizeOfCode = sizeOfCode;
    activeTrace->mThreadId = threadId;
    activeTrace->mBreakpoint = breakpoint;
    activeTrace->mNextLink = trace->mActiveTraces;

    trace->mActiveTraces = activeTrace;

    return eTracerTrue;
}

static TracerBool tracerVeTraceStop(TracerContext* ctx, void* address, int threadId) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContextVEH)) {
        return eTracerFalse;
    }

    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    TracerActiveTrace* prev = NULL;
    TracerActiveTrace* activeTrace = trace->mActiveTraces;

    while (activeTrace) {
        TracerActiveTrace* next = activeTrace->mNextLink;

        if (activeTrace->mStartAddress == address &&
            activeTrace->mThreadId == threadId)
        {
            tracerRemoveHwBreakpoint(activeTrace->mBreakpoint);

            if (prev) {
                prev->mNextLink = next;
            } else {
                trace->mActiveTraces = next;
            }

            free(activeTrace);
            activeTrace = NULL;
        }

        if (activeTrace) {
            prev = activeTrace;
        }
        activeTrace = next;
    }

    return eTracerFalse;
}

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

    int threadId = (int)GetCurrentThreadId();

    TracerActiveTrace* activeTrace = trace->mActiveTraces;
    while (activeTrace) {
        if (address >= activeTrace->mBaseOfCode &&
            address < activeTrace->mBaseOfCode + activeTrace->mSizeOfCode &&
            (activeTrace->mThreadId == -1 || activeTrace->mThreadId == threadId))
        {
            return eTracerFalse;
        }
        activeTrace = activeTrace->mNextLink;
    }

    return eTracerTrue;
}

static TracerBool tracerVeTraceIsTraceStartAddress(TracerContext* ctx, uintptr_t address) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    int threadId = (int)GetCurrentThreadId();

    TracerActiveTrace* activeTrace = trace->mActiveTraces;
    while (activeTrace) {
        if (address == (uintptr_t)activeTrace->mStartAddress &&
            (activeTrace->mThreadId == -1 || activeTrace->mThreadId == threadId)) {

            return eTracerTrue;
        }
        activeTrace = activeTrace->mNextLink;
    }
    return eTracerFalse;
}

static TracerBool tracerVeTraceInstruction(TracerContext* ctx, void* address) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    if (!address) {
        return eTracerTrue;
    }

    ZydisDecodedInstruction decodedInst;
    if (ZydisDecoderDecodeBuffer(&trace->mDecoder, address,
            ZYDIS_MAX_INSTRUCTION_LENGTH, 0, &decodedInst) != ZYDIS_STATUS_SUCCESS) {

        return eTracerFalse;
    }

    TracerTracedInstruction inst;
    inst.mTraceId = tracerCoreGetCurrentTraceId();
    inst.mThreadId = (int)GetCurrentThreadId();
    inst.mAddress = (uintptr_t)address;

    if (ZydisFormatterFormatInstruction(&trace->mFormatter, &decodedInst,
            inst.mDecodedInst, sizeof(inst.mDecodedInst)) != ZYDIS_STATUS_SUCCESS) {

        return eTracerFalse;
    }

    switch (decodedInst.mnemonic) {
    case ZYDIS_MNEMONIC_CALL:
        inst.mType = eTracerNodeTypeCall;
        inst.mCallDepth = tracerCoreOnBranchEntered();
        break;
    case ZYDIS_MNEMONIC_RET:
    case ZYDIS_MNEMONIC_IRET:
    case ZYDIS_MNEMONIC_IRETD:
    case ZYDIS_MNEMONIC_IRETQ:
    case ZYDIS_MNEMONIC_RSM:
        inst.mType = eTracerNodeTypeReturn;
        inst.mCallDepth = tracerCoreOnBranchReturned();
        break;
    default:
        inst.mType = eTracerNodeTypeBranch;
        inst.mCallDepth = tracerCoreGetBranchCallDepth();
    }

    while (!tracerRWQueuePushItem(trace->mSharedRWQueue, &inst)) {
        Sleep(1);
    }

    return inst.mCallDepth >= 0;
}

static LONG CALLBACK tracerVeTraceHandler(PEXCEPTION_POINTERS ex) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)tracerGetLocalProcessContext();

    uintptr_t exceptionAddr = (uintptr_t)ex->ExceptionRecord->ExceptionAddress;
    uintptr_t lastBranchRecord = ex->ExceptionRecord->ExceptionInformation[0];

    switch (ex->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_SINGLE_STEP:
    {
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

            // Check if we have a trace on this address
            if (!tracerVeTraceIsTraceStartAddress(process->mTraceContext, exceptionAddr)) {
                // The interrupt was not triggered by our tracer
                return EXCEPTION_CONTINUE_SEARCH;
            }

            // Temporarily remove the enabled bit for this breakpoint
            // We will restore it on the next call to the handler. The breakpoint index
            // will be stored in thread local storage for the time being.
            tracerHwBreakpointSetBits(&ex->ContextRecord->Dr7, index << 1, 1, 0);

            // This will back up the breakpoint index into the thread local storage and reset the call depth
            tracerCoreOnBeginNewTrace(index);

        } else {

            // Restore the bit that we removed during the first call to the handler
            tracerHwBreakpointSetBits(&ex->ContextRecord->Dr7, index << 1, 1, 1);
        }

        int resumeIndex = tracerCoreGetSuspendedHwBreakpointIndex();

        if (resumeIndex != -1) {
            // Disable the breakpoint that we used to resume the trace
            tracerHwBreakpointSetBits(&ex->ContextRecord->Dr7, resumeIndex << 1, 1, 0);

            // Unset suspended index
            tracerCoreSetSuspendedHwBreakpointIndex(-1);

            // We just resumed from a suspended call, the call depth is therefore 1 call too high
            tracerCoreOnBranchReturned();
        }

        // If this function returns false it means that the tracing for the current
        // thread should be disabled. In this case we remove the branch trace flags.
        if (tracerVeTraceInstruction(process->mTraceContext, (void*)lastBranchRecord)) {

            if (tracerVeTraceIsAddressInExcludedModule(process->mTraceContext, exceptionAddr)) {
                // We are not interested in tracing calls inside windows libraries.

                // We suspend tracing by temporarily adding a hardware breakpoint on the place  that the call
                // will return to. During this time we disable branch tracing completely.

                // Once the resume hardware breakpoint is triggered, the breakpoint is removed and the tracing
                // will continue.
                void* returnAddress = *(void**)ex->ContextRecord->Esp;

                resumeIndex = tracerSetHwBreakpointOnContext(returnAddress,
                    1, ex->ContextRecord, eTracerBpCondExecute);

                // We remember the breakpoint index for this suspended trace by storing it in the threads TLS
                // data.
                tracerCoreSetSuspendedHwBreakpointIndex(resumeIndex);

                // Disable branch tracing for now (until the resume breakpoint triggers)
                tracerVeTraceSetFlags(ex->ContextRecord, eTracerFalse);

            } else {
                // Keep branch tracing on this thread
                tracerVeTraceSetFlags(ex->ContextRecord, eTracerTrue);
            }

        } else {

            // Disable branch tracing on this thread
            tracerVeTraceSetFlags(ex->ContextRecord, eTracerFalse);

            // And remove the stored tls index for the breakpoint
            tracerCoreOnTraceEnded();
        }

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}
