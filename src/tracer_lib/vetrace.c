
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

static TracerBool tracerVeTraceStart(TracerContext* ctx, void* address, int threadId, int maxTraceDepth, int lifetime);

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
    InitializeCriticalSection(&trace->mTraceCritSect);

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

    DeleteCriticalSection(&trace->mTraceCritSect);
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

static TracerBool tracerVeTraceStart(TracerContext* ctx, void* address, int threadId, int maxTraceDepth, int lifetime) {
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
    EnterCriticalSection(&trace->mTraceCritSect);

    activeTrace->mStartAddress = address;
    activeTrace->mBaseOfCode = baseOfCode;
    activeTrace->mSizeOfCode = sizeOfCode;
    activeTrace->mThreadId = threadId;
    activeTrace->mMaxTraceDepth = maxTraceDepth;
    activeTrace->mLifetime = lifetime;
    activeTrace->mBreakpoint = breakpoint;
    activeTrace->mNextLink = trace->mActiveTraces;

    trace->mActiveTraces = activeTrace;
    LeaveCriticalSection(&trace->mTraceCritSect);

    return eTracerTrue;
}

static TracerBool tracerVeTraceStop(TracerContext* ctx, void* address, int threadId) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContextVEH)) {
        return eTracerFalse;
    }

    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;
    EnterCriticalSection(&trace->mTraceCritSect);

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

            if (trace->mCurrentTrace == activeTrace) {
                trace->mCurrentTrace = NULL;
            }

            free(activeTrace);
            activeTrace = NULL;
        }

        if (activeTrace) {
            prev = activeTrace;
        }
        activeTrace = next;
    }

    LeaveCriticalSection(&trace->mTraceCritSect);
    return eTracerFalse;
}

static void tracerVeRemoveCurrentTrace(TracerContext* ctx, PCONTEXT registers) {
    if (!tracerCoreValidateContext(ctx, eTracerTraceContextVEH)) {
        return;
    }

    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;
    TracerActiveTrace* currentTrace = trace->mCurrentTrace;

    TracerActiveTrace* prev = NULL;
    TracerActiveTrace* activeTrace = trace->mActiveTraces;

    while (activeTrace) {
        TracerActiveTrace* next = activeTrace->mNextLink;

        if (activeTrace == currentTrace) {
            tracerRemoveHwBreakpointOnContext(activeTrace->mBreakpoint, registers);

            if (prev) {
                prev->mNextLink = next;
            } else {
                trace->mActiveTraces = next;
            }

            free(activeTrace);
            break;
        }

        prev = activeTrace;
        activeTrace = next;
    }

    trace->mCurrentTrace = NULL;
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

static TracerBool tracerVeShouldSuspendCurrentTrace(TracerContext* ctx, uintptr_t address) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;
    TracerActiveTrace* currentTrace = trace->mCurrentTrace;

    if (!currentTrace) {
        return eTracerTrue;
    }

    TracerBool isAddressWithinModule = 
        (address >= currentTrace->mBaseOfCode &&
         address < currentTrace->mBaseOfCode + currentTrace->mSizeOfCode);

    if (!isAddressWithinModule) {
        return eTracerTrue;
    }

    if (currentTrace->mMaxTraceDepth > 0) {
        // We set a limit to the maximum depth of calls to trace

        if (tracerCoreGetBranchCallDepth() >= currentTrace->mMaxTraceDepth) {
            return eTracerTrue;
        }
    }

    return eTracerFalse;
}

static TracerActiveTrace* tracerVeGetTraceForAddress(TracerContext* ctx, uintptr_t address) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    int threadId = (int)GetCurrentThreadId();

    TracerActiveTrace* activeTrace = trace->mActiveTraces;
    while (activeTrace) {
        if (address == (uintptr_t)activeTrace->mStartAddress &&
            (activeTrace->mThreadId == -1 || activeTrace->mThreadId == threadId)) {

            return activeTrace;
        }
        activeTrace = activeTrace->mNextLink;
    }
    return NULL;
}

static TracerBool tracerVeTraceInstruction(TracerContext* ctx, PEXCEPTION_POINTERS ex, void** resumeAddress) {
    TracerVeTraceContext* trace = (TracerVeTraceContext*)ctx;

    if (!trace->mCurrentTrace) {
        return eTracerFalse;
    }

    uintptr_t lastBranchAddress = ex->ExceptionRecord->ExceptionInformation[0];

    if (!lastBranchAddress) {
        *resumeAddress = ex->ExceptionRecord->ExceptionAddress;
        return eTracerTrue;
    }

    ZydisDecodedInstruction decodedInst;

    if (ZydisDecoderDecodeBuffer(
            &trace->mDecoder,
            (void*)lastBranchAddress,
            ZYDIS_MAX_INSTRUCTION_LENGTH,
            lastBranchAddress,
            &decodedInst) != ZYDIS_STATUS_SUCCESS) {

        return eTracerFalse;
    }

    TracerBool continueTrace = eTracerFalse;

    TracerTracedInstruction inst;
    inst.mTraceId = tracerCoreGetCurrentTraceId();
    inst.mThreadId = (int)GetCurrentThreadId();

    inst.mBranchSource = (uintptr_t)ex->ExceptionRecord->ExceptionInformation[0];
    inst.mBranchTarget = (uintptr_t)ex->ExceptionRecord->ExceptionAddress;

    inst.mRegisterSet.mEAX = ex->ContextRecord->Eax;
    inst.mRegisterSet.mEBX = ex->ContextRecord->Ebx;
    inst.mRegisterSet.mECX = ex->ContextRecord->Ecx;
    inst.mRegisterSet.mEDX = ex->ContextRecord->Edx;
    inst.mRegisterSet.mESI = ex->ContextRecord->Esi;
    inst.mRegisterSet.mEDI = ex->ContextRecord->Edi;
    inst.mRegisterSet.mEBP = ex->ContextRecord->Ebp;
    inst.mRegisterSet.mESP = ex->ContextRecord->Esp;

    inst.mRegisterSet.mSegGS = ex->ContextRecord->SegGs;
    inst.mRegisterSet.mSegFS = ex->ContextRecord->SegFs;
    inst.mRegisterSet.mSegES = ex->ContextRecord->SegEs;
    inst.mRegisterSet.mSegDS = ex->ContextRecord->SegDs;
    inst.mRegisterSet.mSegCS = ex->ContextRecord->SegCs;
    inst.mRegisterSet.mSegSS = ex->ContextRecord->SegSs;

    switch (decodedInst.meta.category) {
    case ZYDIS_CATEGORY_CALL:
        inst.mType = eTracerInstructionTypeCall;
        inst.mCallDepth = tracerCoreOnBranchEntered();
        continueTrace = (inst.mCallDepth >= 0);

        *resumeAddress = *(void**)ex->ContextRecord->Esp;
        break;
    case ZYDIS_CATEGORY_RET:
        inst.mType = eTracerInstructionTypeReturn;
        inst.mCallDepth = tracerCoreOnBranchReturned();
        continueTrace = (inst.mCallDepth > 0);

        *resumeAddress = (void*)ex->ContextRecord->Eip;
        break;
    default:
        inst.mType = eTracerInstructionTypeBranch;
        inst.mCallDepth = tracerCoreGetBranchCallDepth();
        continueTrace = (inst.mCallDepth >= 0);

        *resumeAddress = (void*)ex->ContextRecord->Eip;
    }

    if (ZydisFormatterFormatInstruction(
            &trace->mFormatter,
            &decodedInst,
            inst.mDecodedInst,
            sizeof(inst.mDecodedInst)) != ZYDIS_STATUS_SUCCESS) {

        return eTracerFalse;
    }

    while (!tracerRWQueuePushItem(trace->mSharedRWQueue, &inst)) {
        Sleep(1);
    }

    return continueTrace;
}

static LONG CALLBACK tracerVeTraceHandler(PEXCEPTION_POINTERS ex) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)tracerGetLocalProcessContext();

    TracerVeTraceContext* trace = (TracerVeTraceContext*)process->mTraceContext;

    uintptr_t exceptionAddr = (uintptr_t)ex->ExceptionRecord->ExceptionAddress;

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

            EnterCriticalSection(&trace->mTraceCritSect);

            // Get the trace for this address
            TracerActiveTrace* activeTrace = tracerVeGetTraceForAddress(
                process->mTraceContext, exceptionAddr);

            if (!activeTrace) {
                LeaveCriticalSection(&trace->mTraceCritSect);

                // The interrupt was not triggered by our tracer
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (trace->mCurrentTrace) {
                LeaveCriticalSection(&trace->mTraceCritSect);

                // Some other thread is currently running a trace, ignore this
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            trace->mCurrentTrace = activeTrace;
            LeaveCriticalSection(&trace->mTraceCritSect);

            // Temporarily remove the enabled bit for this breakpoint
            // We will set this bit again on the next call to this handler (else part of this branch)
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

        void* resumeAddr = NULL;

        // If this function returns false it means that the tracing for the current
        // thread should be disabled. In this case we remove the branch trace flags.
        if (tracerVeTraceInstruction(process->mTraceContext, ex, &resumeAddr)) {

            if (tracerVeShouldSuspendCurrentTrace(process->mTraceContext, exceptionAddr)) {
                // We are not interested in tracing calls inside windows libraries.

                // We suspend tracing by temporarily adding a hardware breakpoint on the place  that the call
                // will return to. During this time we disable branch tracing completely.

                // Once the resume hardware breakpoint is triggered, the breakpoint is removed and the tracing
                // will continue.
                resumeIndex = tracerSetHwBreakpointOnContext(resumeAddr, 1, ex->ContextRecord, eTracerBpCondExecute);

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

            // Each trace has a lifetime field. If the lifetime field is set and reaches 0, the trace
            // for this function should be removed.

            EnterCriticalSection(&trace->mTraceCritSect);

            TracerActiveTrace* currentTrace = trace->mCurrentTrace;

            if (currentTrace && currentTrace->mLifetime > 0 && --currentTrace->mLifetime == 0) {
                tracerVeRemoveCurrentTrace(process->mTraceContext, ex->ContextRecord);
            }

            // The current trace has ended
            trace->mCurrentTrace = NULL;

            LeaveCriticalSection(&trace->mTraceCritSect);

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
