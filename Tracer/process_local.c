#include "process_local.h"
#include "memory_local.h"

#include <stdio.h>
#include <assert.h>

static TracerBool tracerProcessLocalInit(TracerContext* ctx);

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx);

static void tracerSetTraceFlags(PCONTEXT context, TracerBool enable);

static TracerBool tracerSetTraceFlagsOnThread(DWORD threadId, TracerBool enable);

static LONG CALLBACK tracerVectoredExceptionHandler(PEXCEPTION_POINTERS ex);

static TracerContext* gTracerLocalProcessContext = NULL;

TracerContext* tracerCreateLocalProcessContext(int type, int size) {
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

    process->mMemoryContext = tracerCreateLocalMemoryContext(
        eTracerMemoryContextLocal, sizeof(TracerLocalMemoryContext));

    if (!process->mMemoryContext) {
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
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    process->mLogFile = CreateFileW(L"trace.log", GENERIC_WRITE,
        FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

    process->mVehHandle = AddVectoredExceptionHandler(TRUE, tracerVectoredExceptionHandler);

    // icebp
    __asm _emit 0xf1

    return eTracerTrue;
}

static TracerBool tracerProcessLocalShutdown(TracerContext* ctx) {
    TracerLocalProcessContext* process = (TracerLocalProcessContext*)ctx;

    RemoveVectoredExceptionHandler(process->mVehHandle);
    process->mVehHandle = NULL;

    CloseHandle(process->mLogFile);
    process->mLogFile = NULL;

    return eTracerTrue;
}

#define TLIB_TRFLAG_DR7_LBR                 0x100       // Last Branch Record (Bit 8 in DR7)
														// Mapped to Model Specific Register in Kernel (MSR).

#define TLIB_TRFLAG_DR7_BTF                 0x200       // Branch Trap Flag (Bit 9 in DR7)
                                                        // Enables trap on next branch
                                                        // Mapped to Model Specific Register in Kernel (MSR).

#define TLIB_TRFLAG_EFLAGS_SINGLE_STEP      0x100       // Single Step Flag (Trap on next instruction)

static void tracerSetTraceFlags(PCONTEXT context, TracerBool enable) {
	// Enable or disable the trace flags for the given thread context

    if (enable) {
        context->Dr7 |= (TLIB_TRFLAG_DR7_LBR | TLIB_TRFLAG_DR7_BTF);
        context->EFlags |= TLIB_TRFLAG_EFLAGS_SINGLE_STEP;
    } else {
        context->Dr7 &= ~(TLIB_TRFLAG_DR7_LBR | TLIB_TRFLAG_DR7_BTF);
        context->EFlags &= ~TLIB_TRFLAG_EFLAGS_SINGLE_STEP;
    }
}

static TracerBool tracerSetTraceFlagsOnThread(DWORD threadId, TracerBool enable) {

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
                tracerSetTraceFlags(&context, enable);

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

static LONG CALLBACK tracerVectoredExceptionHandler(PEXCEPTION_POINTERS ex) {
    uint64_t branchFrom = 0;
    uint64_t branchTo = 0;

    TracerLocalProcessContext* process = (TracerLocalProcessContext*)tracerGetLocalProcessContext();

    switch (ex->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_SINGLE_STEP:
        branchFrom = ex->ExceptionRecord->ExceptionInformation[0];
        branchTo = ex->ContextRecord->Eip;

		if (branchFrom && branchTo) {
			uint8_t branchFromInsn = *(uint8_t*)branchFrom;
			uint8_t branchToInsn = *(uint8_t*)branchTo;

			char buffer[256];
			sprintf(buffer, "Branch from 0x%08llX (%02X) to 0x%08llX (%02X)\r\n",
				branchFrom, branchFromInsn, branchTo, branchToInsn);

            MessageBoxA(NULL, buffer, "Branch", MB_OK);

			DWORD numBytesWritten = 0;
			WriteFile(process->mLogFile, buffer, strlen(buffer), &numBytesWritten, NULL);
		}

        tracerSetTraceFlags(ex->ContextRecord, eTracerTrue);
        return EXCEPTION_CONTINUE_EXECUTION;

    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}
