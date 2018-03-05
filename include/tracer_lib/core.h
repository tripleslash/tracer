
#ifndef TLIB_CORE_H
#define TLIB_CORE_H

#define TLIB_CORE_COMPILING_DLL 1

#include <tracer_lib/tracer_lib.h>

#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#define WIN32_LEAN_AND_MEAN 1
#define _CRT_SECURE_NO_WARNINGS 1

#include <Windows.h>

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#ifdef _UNICODE
typedef wchar_t tchar;
#else
typedef char tchar;
#endif

#define TLIB_METHOD_CHECK_SUPPORT(function, ...) \
    if (!function) { \
        tracerCoreSetLastError(eTracerErrorNotImplemented); \
        return __VA_ARGS__; \
    }

typedef struct TracerBaseContext {
    int                              mSizeOfStruct;
    int                              mTypeFlags;

    struct TracerBaseContext*        mNextLink;
    struct TracerBaseContext*        mPrevLink;

    void(*mCleanup)(TracerContext* ctx);
} TracerBaseContext;

typedef enum TracerContextClassType {
    eTracerProcessContext            = 0x00000010,
    eTracerProcessContextLocal       = eTracerProcessContext | 0x00000001,
    eTracerProcessContextRemote      = eTracerProcessContext | 0x00000002,

    eTracerMemoryContext             = 0x00000020,
    eTracerMemoryContextLocal        = eTracerMemoryContext | 0x00000001,
    eTracerMemoryContextRemote       = eTracerMemoryContext | 0x00000002,

    eTracerTraceContext              = 0x00000040,
    eTracerTraceContextVEH           = eTracerTraceContext | 0x00000001,
} TracerContextClassType;

TracerContext* tracerCoreCreateContext(int type, int size);

void tracerCoreCleanupContext(TracerContext* ctx);

void tracerCoreDestroyContext(TracerContext* ctx);

TracerBool tracerCoreValidateContext(TracerContext* ctx, int type);

int tracerCoreGetContextTypeFlags(TracerContext* ctx);

typedef TracerBool(*TracerContextCallback)(TracerContext*, void*);

TracerBool tracerCoreEnumContexts(const TracerContextCallback cb, void* parameter, int type, TracerBool returnAfterFail);

/*
 *
 * Core utility functions (getter, setter...)
 *
 */

void tracerCoreSetLastError(TracerError error);

TracerError tracerCoreGetLastError();

void tracerCoreSetProcessContext(TracerContext* ctx);

TracerContext* tracerCoreGetProcessContext();

TracerHandle tracerCoreGetModuleHandle();

int tracerCoreGetActiveHwBreakpointIndex();

void tracerCoreSetActiveHwBreakpointIndex(int index);

int tracerCoreGetSuspendedHwBreakpointIndex();

void tracerCoreSetSuspendedHwBreakpointIndex(int index);

TracerHandle tracerCoreFindWindow(int processId);

TracerBool tracerCoreSetPrivilege(TracerHandle process, const tchar* privilege, TracerBool enable);

/*
 *
 * Hash table based process context management functions
 *
 */

TracerContext* tracerCoreGetContextForPID(int pid);

void tracerCoreSetContextForPID(int pid, TracerContext* ctx);

TracerBool tracerCoreEnumProcessContexts(const TracerContextCallback cb, void* parameter, TracerBool returnAfterFail);

void tracerCoreAcquireProcessContextLock();

void tracerCoreReleaseProcessContextLock();

#endif
