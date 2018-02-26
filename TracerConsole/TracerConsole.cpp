// TracerConsole.cpp: Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include "stdafx.h"
#include "tracer_api.h"

#include <iostream>
#include <string>
#include <sstream>

#include <Windows.h>
#include <Psapi.h>

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

int main()
{
    printProcessOverview();

    tracerInit();

    std::cout << "Tracer v" << tracerGetVersion() << std::endl;

    std::cout << std::endl;
    std::cout << "Enter process id: ";

    std::string spid;
    std::getline(std::cin, spid);

    int pid;
    std::stringstream ss(spid);

    if ((ss >> pid) && tracerAttachProcess(pid)) {
        std::cout << "Attached to process with id " << pid << "." << std::endl;
    } else {
        std::cerr << "Can't attach to process with id " << pid << "." << std::endl;
        std::cerr << "Last error: " << tracerErrorToString(tracerGetLastError()) << std::endl;
    }

    tracerStartTrace(NULL);
    tracerStopTrace(NULL);

    std::cin.clear();
    std::cin.sync();
    std::cin.get();
    
    tracerShutdown();

    return 0;
}
