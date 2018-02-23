#ifndef TRACER_H
#define TRACER_H

#define TLIB_VERSION                    100

#if defined(_MSC_VER)
	#define TLIB_DECL(...)              __declspec(__VA_ARGS__)
	#define TLIB_CALL					__stdcall
#elif defined(__GNUC__)
	#define TLIB_DECL(...)              __attribute__((__VA_ARGS__))
	#define TLIB_CALL                   __attribute__((stdcall))
#else
	#error Compiler not supported!
#endif

#if TLIB_CORE_COMPILING_DLL
    #define TLIB_API                    TLIB_DECL(dllexport)
#else
    #define TLIB_API                    TLIB_DECL(dllimport)
    #pragma comment(lib, "tracer")
#endif

#ifdef __cplusplus
    #define TLIB_ARG(v)               = v
#else
    #define TLIB_ARG(v)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   A type used for holding internal handle values.
 */
typedef void* TracerHandle;

/**
 * @brief   Values that represent a \c bool in C.
 */
enum TracerBoolean {
    eTracerFalse                        = 0,    ///< Equals \c false.
    eTracerTrue                         = 1,    ///< Equals \c true.
};

/**
 * @brief   A type used for holding boolean values (equals \c bool in C++).
 */
typedef char TracerBool;

/**
 * @brief   Values that represent possible error codes.
 * @see     tracerGetLastError
 */
typedef enum TracerError {
    eTracerErrorSuccess                 = 0,    ///< The operation completed successfully.
    eTracerErrorWrongVersion            = 1,    ///< The library version does not match the representation in this header file.
    eTracerErrorNotImplemented          = 2,    ///< The operation failed because it is not currently implemented.
    eTracerErrorInvalidArgument         = 3,    ///< The operation failed due to an invalid argument.
    eTracerErrorInvalidProcess          = 4,    ///< The operation failed due to an invalid process id.
    eTracerErrorInvalidHandle           = 5,    ///< The operation failed due to an invalid handle.
    eTracerErrorInsufficientPermission  = 6,    ///< The operation failed due to insufficient permission.
    eTracerErrorNotEnoughMemory         = 7,    ///< The operation failed because there is not enough memory available.
    eTracerErrorSystemCall              = 8,    ///< The operation failed because a system call returned an error.
    eTracerErrorWaitTimeout             = 9,    ///< The operation timed out due to a user specified timeout parameter.
    eTracerErrorWaitIncomplete          = 10,   ///< The operation failed because one of the wait handles returned an error.
    eTracerErrorRemoteInterop           = 11,   ///< The operation failed because the remote end returned an error.
    eTracerErrorPatternsNotFound        = 12,   ///< The operation failed because one of the patterns could not be found.
} TracerError;

// Specify structure packing to prevent padding mismatches.
#pragma pack(push, 4)

/**
 * @brief   Internal helper type. Used as basis for all structures.
 */
typedef struct TracerStruct {
    int             mSizeOfStruct;              ///< The size of the structure (in bytes).
} TracerStruct;

/**
 * @brief   The structure that should be passed to \ref tracerInitEx.
 * @remarks Don't forget to set \ref mSizeOfStruct.
 * @see     tracerInit
 * @see     tracerInitEx
 */
typedef struct TracerInit {
    int             mSizeOfStruct;              ///< The size of the structure (in bytes).
    int             mVersion;                   ///< Should be set to \ref TLIB_VERSION.
    TracerBool      mAcquireSeDebugPrivilege;   ///< Set this to \ref eTracerFalse if you don't want the
                                                ///< library to request the SeDebugPrivilege.
} TracerInit;

/**
 * @brief   The structure that should be passed to \ref tracerShutdownEx.
 * @remarks Don't forget to set \ref mSizeOfStruct.
 * @see     tracerShutdown
 * @see     tracerShutdownEx
 */
typedef struct TracerShutdown {
    int             mSizeOfStruct;              ///< The size of the structure (in bytes).
} TracerShutdown;

/**
 * @brief   The structure that should be passed to \ref tracerAttachProcessEx.
 * @remarks Don't forget to set \ref mSizeOfStruct.
 * @see     tracerAttachProcess
 * @see     tracerAttachProcessEx
 */
typedef struct TracerAttachProcess {
    int             mSizeOfStruct;              ///< The size of the structure (in bytes).
    int             mProcessId;                 ///< The process id to which we should attach to.
                                                ///< Set this to \c -1 to attach to the current process.
} TracerAttachProcess;

/**
 * @brief   The structure that should be passed to \ref tracerSetHotkeys.
 * @remarks If you set one of the keys to \c 0, the key will be disabled.
 *          Don't forget to set \ref mSizeOfStruct.
 * @see     tracerSetHotkeys
 */
typedef struct TracerHotkeySet {
    int             mSizeOfStruct;              ///< The size of the structure (in bytes).
} TracerHotkeySet;

/**
* @brief   A context is the equivalent to a class in this lib.
*/
typedef TracerStruct TracerContext;

// Restore original structure packing.
#pragma pack(pop)

/**
* @brief   Initializes the library.
*
* Must be called once before you make any other API calls.
*
* @param   version         Should be \ref TLIB_VERSION.
* @retval  eTracerTrue     The function succeeded.
* @retval  eTracerFalse    The function failed.
* @remarks To get extended error information, call \ref tracerGetLastError.
* @see     tracerInitEx
*/
TLIB_API TracerBool TLIB_CALL tracerInit(int version TLIB_ARG(TLIB_VERSION));

/**
* @brief   Initializes the library.
*
* Must be called once before you make any other API calls.
* This function allows specifying extended parameters through \c init.
*
* @param   init            See \ref TracerInit.
* @retval  eTracerTrue     The function succeeded.
* @retval  eTracerFalse    The function failed.
* @remarks To get extended error information, call \ref tracerGetLastError.
* @see     tracerInit
*/
TLIB_API TracerBool TLIB_CALL tracerInitEx(TracerInit* init);

/**
* @brief   Uninitializes the library.
*
* This function first detaches all processes before shutting down the library.
* Must be called after you're done using the library, otherwise the behaviour is undefined.
*
* @retval  eTracerTrue     The function succeeded.
* @retval  eTracerFalse    The function failed.
* @remarks To get extended error information, call \ref tracerGetLastError.
* @see     tracerShutdownEx
*/
TLIB_API TracerBool TLIB_CALL tracerShutdown(void);

/**
* @brief   Uninitializes the library.
*
* This function first detaches all processes before shutting down the library.
* Must be called after you're done using the library, otherwise the behaviour is undefined.
* This function allows specifying extended parameters through \c shutdown.
*
* @param   shutdown        See \ref TracerShutdown.
* @retval  eTracerTrue     The function succeeded.
* @retval  eTracerFalse    The function failed.
* @remarks To get extended error information, call \ref tracerGetLastError.
* @see     tracerShutdown
*/
TLIB_API TracerBool TLIB_CALL tracerShutdownEx(TracerShutdown* shutdown);

/**
* @brief   Retrieves the version number of the DLL.
*
* The least 2 significant digits define minor version (e.g. 152 => 1.52).
*
* @return  The version number of the DLL.
* @see     TLIB_VERSION
*/
TLIB_API int TLIB_CALL tracerGetVersion(void);

/**
* @brief   Retrieves the last error code value for the calling thread.
* @return  One of the error codes in \ref TracerError.
* @see     TracerError
*/
TLIB_API TracerError TLIB_CALL tracerGetLastError(void);

/**
* @brief   Formats an error code into a user readable string.
* @param   error           One of the error codes in \ref TracerError.
* @return  A user readable string.
* @see     TracerError
*/
TLIB_API const char* TLIB_CALL tracerErrorToString(TracerError error);

/**
* @brief   Attaches the library to the given process.
*
* In case you want to use this library inside an injected DLL
* pass \c -1 for the \c pid parameter to attach to the current process.
*
* @param   pid             The process id. Pass \c -1 to attach to the current process.
* @return  A process context that can be passed to \ref tracerSetProcessContext
*          if the function succeeds. Otherwise \c NULL.
* @remarks Don't forget to call \ref tracerDetachProcess when you're done.
*          To get extended error information, call \ref tracerGetLastError.
* @see     tracerAttachProcessEx
* @see     tracerSetProcessContext
* @see     tracerDetachProcess
*/
TLIB_API TracerContext* TLIB_CALL tracerAttachProcess(int pid);

/**
* @brief   Attaches the library to the given process.
*
* This function allows specifying extended parameters through \c attach.
*
* @param   attach          See \ref TracerAttachProcess.
* @return  A process context that can be passed to \ref tracerSetProcessContext,
*          if the function succeeds. Otherwise \c NULL.
* @remarks Don't forget to call \ref tracerDetachProcess when you're done.
*          To get extended error information, call \ref tracerGetLastError.
* @see     tracerAttachProcess
* @see     tracerSetProcessContext
* @see     tracerDetachProcess
*/
TLIB_API TracerContext* TLIB_CALL tracerAttachProcessEx(TracerAttachProcess* attach);

/**
* @brief   Detaches the library from the given process.
*
* After this call the process context \c ctx is invalid and should not be used.
*
* @param   ctx             The process context.
* @retval  eTracerTrue     The function succeeded.
* @retval  eTracerFalse    The function failed.
* @remarks To detach all processes at once, you can pass \c NULL.
*          To get extended error information, call \ref tracerGetLastError.
* @see     tracerAttachProcess
* @see     tracerAttachProcessEx
*/
TLIB_API TracerBool TLIB_CALL tracerDetachProcess(TracerContext* ctx);

/**
* @brief   Sets the current process context for the calling thread.
*
* Calls to functions like \ref tracerSetHotkeys will be executed in the process
* that belongs to this context.
*
* @param   ctx             The process context.
* @remarks You can set the context to \c NULL to distribute calls among all attached
*          processes.
* @see     tracerGetProcessContext
*/
TLIB_API void TLIB_CALL tracerSetProcessContext(TracerContext* ctx);

/**
* @brief   Gets the current process context for the calling thread.
*
* Calls to functions like \ref tracerSetHotkeys will be executed in the process
* that belongs to this context.
*
* @param   ctx             The process context.
* @remarks If the process context is set to \c NULL, the calls are distributed among
*          all attached processes.
* @see     tracerSetProcessContext
*/
TLIB_API TracerContext* TLIB_CALL tracerGetProcessContext(void);

/**
* @brief   Gets the process context for the given process id.
* @param   pid             The process id for which the context should be retrieved.
* @return  The process context for this process id or NULL if the process is not attached.
* @see     tracerAttachProcess
*/
TLIB_API TracerContext* TLIB_CALL tracerGetContextForPid(int pid);

/**
* @brief   Sets the hotkeys for the current process context.
* @param   hotkeys         See \ref TracerHotkeySet.
* @retval  eTracerTrue     The function succeeded.
* @retval  eTracerFalse    The function failed.
* @remarks If the process context is \c NULL, the hotkeys for all attached processes
*          are updated.
*          To get extended error information, call \ref tracerGetLastError.
* @see     tracerSetProcessContext
*/
TLIB_API TracerBool TLIB_CALL tracerSetHotkeys(TracerHotkeySet* hotkeys);

#ifdef __cplusplus
}
#endif

#endif
