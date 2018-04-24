// TracerConsole.cpp: Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include <tracer_lib/tracer_lib.h>

#include <iostream>
#include <string>
#include <thread>
#include <sstream>

#include <Windows.h>
#include <Psapi.h>

#define MAX_TRACES 512




static int cmpfunc(const void* a, const void* b) {
    return (*(int*)a - *(int*)b);
}

int N = 0;
constexpr int MaxN = 1000;

int Values[MaxN] = { 0 };
int InitValues[MaxN] = { 0 };

void initArray() {
    for (int i = 0; i < MaxN; ++i) {
        InitValues[i] = rand();
    }
}

void setup() {
    memcpy(Values, InitValues, sizeof(Values));
}

static void __declspec(noinline) testTrace(int n)
{
    qsort(Values, n, sizeof(int), cmpfunc);
}

void example()
{
    srand(1337);
    initArray();

    LARGE_INTEGER freql;
    QueryPerformanceFrequency(&freql);

    double freq = double(freql.QuadPart / 1000.0);

    LONGLONG elapsed[MaxN] = { 0 };

    tracerInit();
    tracerSetProcessContext(tracerAttachProcess(-1));

    tracerStartTrace(testTrace, -1, -1);

    for (int i = 0; i < MaxN; ++i)
    {
        setup();

        LARGE_INTEGER s1, s2;
        QueryPerformanceCounter(&s1);
        testTrace(i);
        QueryPerformanceCounter(&s2);

        elapsed[i] = (s2.QuadPart - s1.QuadPart);
    }

    tracerStopTrace(testTrace);

    TracerTracedInstruction traces[MAX_TRACES];
    tracerFetchTraces(traces, MAX_TRACES);

    printf("[");
    for (int i = 0; i < MaxN; ++i)
        printf("%Lf,", (elapsed[i] / freq));
    printf("]\n");

    return;

    //puts("");
    //puts("========================");
    //puts("Trace results:");
    //puts("========================");
    //puts("");

    //TracerTracedInstruction traces[MAX_TRACES];

    //size_t numTraces = tracerFetchTraces(traces, MAX_TRACES);

    //for (size_t i = 0; i < numTraces; ++i) {
    //    char decodedInst[64];

    //    if (tracerFormatInstruction(traces[i].mBranchSource, decodedInst, sizeof(decodedInst))) {
    //        printf("%08X %*s %s\n", traces[i].mBranchSource, traces[i].mCallDepth, "", decodedInst);
    //    }
    //}

    //tracerShutdown();
}
















void printProcessNameAndId(DWORD pid) {

    DWORD accessFlag = PROCESS_QUERY_INFORMATION
        | PROCESS_VM_READ;

    HANDLE process = OpenProcess(accessFlag, FALSE, pid);

    wchar_t processName[MAX_PATH] = L"<unknown>";

    bool print = false;

    if (process) {
        HMODULE module;
        DWORD cbneeded;

        if (EnumProcessModules(process, &module, sizeof(module), &cbneeded)) {
            print = GetModuleBaseName(process, module, processName, MAX_PATH) != 0;
        }

        CloseHandle(process);
    }

    if (print) {
        std::wcout << processName << L" (PID: " << pid << L")." << std::endl;
    }
}

void printProcessOverview() {
    DWORD processes[1024];
    DWORD cbneeded;

    if (!EnumProcesses(processes, sizeof(processes), &cbneeded)) {
        return;
    }

    DWORD numProcesses = cbneeded / sizeof(DWORD);

    std::cout << "Process overview - " << numProcesses << " processes:" << std::endl;
    std::cout << "===================================" << std::endl;

    for (int i = 0; i < (int)numProcesses; ++i) {
        if (processes[i] != 0) {
            printProcessNameAndId(processes[i]);
        }
    }

    std::cout << "===================================" << std::endl;
}

//static void __declspec(noinline) testTrace(void) {
//    printf("hello world\n");
//}

int main()
{
    example();

    //printProcessOverview();

    //tracerInit();

    //std::cout << "Tracer v" << tracerGetVersion() << std::endl;

    //std::cout << std::endl;
    //std::cout << "Enter process id: ";

    //std::string spid;
    //std::getline(std::cin, spid);

    //int pid;
    //std::stringstream ss(spid);

    //if ((ss >> pid) && tracerAttachProcess(pid)) {
    //    std::cout << "Attached to process with id " << pid << "." << std::endl;
    //} else {
    //    std::cerr << "Can't attach to process with id " << pid << "." << std::endl;
    //    std::cerr << "Last error: " << tracerErrorToString(tracerGetLastError()) << std::endl;
    //}

    //tracerSetProcessContext(tracerGetContextForPid(pid));

    //tracerStartTrace(testTrace);
    //{
    //    testTrace();
    //}
    //tracerStopTrace(testTrace);

    //TracerTracedInstruction traces[MAX_TRACES];
    //size_t numTraces = tracerFetchTraces(traces, MAX_TRACES);

    //for (size_t i = 0; i < numTraces; ++i) {
    //    char decodedInst[64];

    //    if (tracerFormatInstruction(traces[i].mBranchSource, decodedInst, sizeof(decodedInst))) {
    //        printf("%08X %*s %s\n", traces[i].mBranchSource, traces[i].mCallDepth, "", decodedInst);
    //    }
    //}
    //
    //tracerShutdown();

    std::cin.get();
    return 0;
}

