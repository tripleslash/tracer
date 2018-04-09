
#include <tracer_lib/symbol_resolver.h>

#include <Zydis/Zydis.h>

#include <DbgHelp.h>
#include <stdio.h>

#pragma comment(lib, "dbghelp.lib")

/**
 * @brief   Appends formatted text to the given `string`.
 *
 * @param   string  A pointer to the string.
 * @param   format  The format string.
 *
 * @return  @c ZYDIS_STATUS_SUCCESS, if the function succeeded, or
 *          @c ZYDIS_STATUS_INSUFFICIENT_BUFFER_SIZE, if the size of the buffer was not
 *          sufficient to append the given text.
 */
static ZYDIS_INLINE ZydisStatus ZydisStringAppendFormatC(ZydisString* string, const char* format, ...) {
    if (!string || !string->buffer || !format) {
        return ZYDIS_STATUS_INVALID_PARAMETER;
    }

    va_list arglist;
    va_start(arglist, format);
    const int w = vsnprintf(string->buffer + string->length, string->capacity - string->length,
        format, arglist);

    if ((w < 0) || ((size_t)w > string->capacity - string->length)) {
        va_end(arglist);
        return ZYDIS_STATUS_INSUFFICIENT_BUFFER_SIZE;
    }

    string->length += w;
    va_end(arglist);
    return ZYDIS_STATUS_SUCCESS;
}

static ZydisFormatterAddressFunc formatAddressOriginal;

static ZydisStatus ZydisFormatterPrintAddressWithSymbols(const ZydisFormatter* formatter,
    ZydisString* string, const ZydisDecodedInstruction* instruction,
    const ZydisDecodedOperand* operand, ZydisU64 address, void* userData)
{
    ZYDIS_UNUSED_PARAMETER(operand);
    ZYDIS_UNUSED_PARAMETER(userData);

    if (!formatter || !instruction) {
        return ZYDIS_STATUS_INVALID_PARAMETER;
    }

    SYMBOL_INFO_PACKAGE symbolInfoPackage;
    symbolInfoPackage.si.SizeOfStruct = sizeof(SYMBOL_INFO);
    symbolInfoPackage.si.MaxNameLen = sizeof(symbolInfoPackage.name);

    DWORD64 displacement = 0;
    if (!SymFromAddr(GetCurrentProcess(), address, &displacement, &symbolInfoPackage.si)) {
        return formatAddressOriginal(formatter, string, instruction, operand, address, userData);
    }

    if (!displacement) {
        return ZydisStringAppendFormatC(string, "%s", symbolInfoPackage.si.Name);
    } else {
        return ZydisStringAppendFormatC(string, "%s+%X", symbolInfoPackage.si.Name, displacement);
    }
}

TracerBool tracerRegisterCustomSymbolResolver(void* instructionFormatter) {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);

    formatAddressOriginal = (ZydisFormatterAddressFunc)&ZydisFormatterPrintAddressWithSymbols;

    if (ZydisFormatterSetHook((ZydisFormatter*)instructionFormatter,
            ZYDIS_FORMATTER_HOOK_PRINT_ADDRESS,
            (const void**)&formatAddressOriginal) != ZYDIS_STATUS_SUCCESS) {

        return eTracerFalse;
    }

    return eTracerTrue;
}

TracerBool tracerUnregisterCustomSymbolResolver() {
    SymCleanup(GetCurrentProcess());
    return eTracerTrue;
}
