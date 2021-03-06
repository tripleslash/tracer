
#include <stdio.h>
#include <Windows.h>

extern "C"
{
    void __declspec(noinline, dllexport) testFunction(int i)
    {
        printf("hello world: %d\n", i);
    }
}

int main()
{
    for (int i = 0; i < 100000; ++i)
    {
        testFunction(i);
        Sleep(1000);
    }
}

//// TracerConsole.cpp: Definiert den Einstiegspunkt für die Konsolenanwendung.
////
//
//#include <tracer_lib/tracer_lib.h>
//
//#include <iostream>
//#include <string>
//#include <thread>
//#include <sstream>
//
//#include <Windows.h>
//#include <Psapi.h>
//
//#define MAX_TRACES 512
//
//
//static int __declspec(noinline) fakultaet(int x)
//{
//    if (x > 1) {
//        return x * fakultaet(x - 1);
//    } else {
//        return 1;
//    }
//}
//
//static void __declspec(noinline) testTrace(int n)
//{
//    if (n % 2 == 0) {
//        puts("number is even.");
//    } else {
//        printf("number is not even! number was %d.\n", n);
//    }
//}
//
//void example()
//{
//    tracerInit();
//    tracerSetProcessContext(tracerAttachProcess(-1));
//
//    tracerStartTrace(fakultaet, -1, -1);
//    {
//        for (int i = 0; i < 10; ++i)
//            printf("fakultaet: %d\n", fakultaet(i));
//    }
//    tracerStopTrace(fakultaet);
//
//    puts("");
//    puts("========================");
//    puts("Trace results:");
//    puts("========================");
//    puts("");
//
//    TracerTracedInstruction traces[MAX_TRACES];
//
//    size_t numTraces = tracerFetchTraces(traces, MAX_TRACES);
//
//    for (size_t i = 0; i < numTraces; ++i) {
//        char decodedInst[64];
//
//        if (tracerDecodeAndFormatInstruction(traces[i].mBranchSource, decodedInst, sizeof(decodedInst))) {
//            printf("%08X %*s %s\n", traces[i].mBranchSource, traces[i].mCallDepth, "", decodedInst);
//        }
//    }
//
//    tracerShutdown();
//}
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//void printProcessNameAndId(DWORD pid) {
//
//    DWORD accessFlag = PROCESS_QUERY_INFORMATION
//        | PROCESS_VM_READ;
//
//    HANDLE process = OpenProcess(accessFlag, FALSE, pid);
//
//    wchar_t processName[MAX_PATH] = L"<unknown>";
//
//    bool print = false;
//
//    if (process) {
//        HMODULE module;
//        DWORD cbneeded;
//
//        if (EnumProcessModules(process, &module, sizeof(module), &cbneeded)) {
//            print = GetModuleBaseName(process, module, processName, MAX_PATH) != 0;
//        }
//
//        CloseHandle(process);
//    }
//
//    if (print) {
//        std::wcout << processName << L" (PID: " << pid << L")." << std::endl;
//    }
//}
//
//void printProcessOverview() {
//    DWORD processes[1024];
//    DWORD cbneeded;
//
//    if (!EnumProcesses(processes, sizeof(processes), &cbneeded)) {
//        return;
//    }
//
//    DWORD numProcesses = cbneeded / sizeof(DWORD);
//
//    std::cout << "Process overview - " << numProcesses << " processes:" << std::endl;
//    std::cout << "===================================" << std::endl;
//
//    for (int i = 0; i < (int)numProcesses; ++i) {
//        if (processes[i] != 0) {
//            printProcessNameAndId(processes[i]);
//        }
//    }
//
//    std::cout << "===================================" << std::endl;
//}
//
////static void __declspec(noinline) testTrace(void) {
////    printf("hello world\n");
////}
//
//int main()
//{
//    example();
//
//    //printProcessOverview();
//
//    //tracerInit();
//
//    //std::cout << "Tracer v" << tracerGetVersion() << std::endl;
//
//    //std::cout << std::endl;
//    //std::cout << "Enter process id: ";
//
//    //std::string spid;
//    //std::getline(std::cin, spid);
//
//    //int pid;
//    //std::stringstream ss(spid);
//
//    //if ((ss >> pid) && tracerAttachProcess(pid)) {
//    //    std::cout << "Attached to process with id " << pid << "." << std::endl;
//    //} else {
//    //    std::cerr << "Can't attach to process with id " << pid << "." << std::endl;
//    //    std::cerr << "Last error: " << tracerErrorToString(tracerGetLastError()) << std::endl;
//    //}
//
//    //tracerSetProcessContext(tracerGetContextForPid(pid));
//
//    //tracerStartTrace(testTrace);
//    //{
//    //    testTrace();
//    //}
//    //tracerStopTrace(testTrace);
//
//    //TracerTracedInstruction traces[MAX_TRACES];
//    //size_t numTraces = tracerFetchTraces(traces, MAX_TRACES);
//
//    //for (size_t i = 0; i < numTraces; ++i) {
//    //    char decodedInst[64];
//
//    //    if (tracerFormatInstruction(traces[i].mBranchSource, decodedInst, sizeof(decodedInst))) {
//    //        printf("%08X %*s %s\n", traces[i].mBranchSource, traces[i].mCallDepth, "", decodedInst);
//    //    }
//    //}
//    //
//    //tracerShutdown();
//
//    std::cin.get();
//    return 0;
//}
//
