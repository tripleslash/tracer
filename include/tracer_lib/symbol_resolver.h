
#ifndef SYMBOL_RESOLVER_H
#define SYMBOL_RESOLVER_H

#include <tracer_lib/core.h>

TracerBool tracerRegisterCustomSymbolResolver(void* instructionFormatter);

TracerBool tracerUnregisterCustomSymbolResolver();

uintptr_t tracerResolveSymbol(const char* symbolName);

#endif
