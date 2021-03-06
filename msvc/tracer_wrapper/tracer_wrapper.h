#pragma once

#include <tracer_lib/tracer_lib.h>

namespace tracer_wrapper
{
    public enum class TracedInstructionType
    {
        Branch = 0,
        Call = 1,
        Return = 2,
    };

    public value struct RegisterSetX86
    {
        System::UIntPtr Eax;
        System::UIntPtr Ebx;
        System::UIntPtr Ecx;
        System::UIntPtr Edx;
        System::UIntPtr Esi;
        System::UIntPtr Edi;
        System::UIntPtr Ebp;
        System::UIntPtr Esp;

        System::UIntPtr SegGs;
        System::UIntPtr SegFs;
        System::UIntPtr SegEs;
        System::UIntPtr SegDs;
        System::UIntPtr SegCs;
        System::UIntPtr SegSs;
    };

    public value struct TracedInstruction
    {
        TracedInstructionType Type;

        int TraceId;
        int ThreadId;
        int CallDepth;

        System::UIntPtr BranchSource;
        System::UIntPtr BranchTarget;

        RegisterSetX86 RegisterSet;
    };

    public ref class ApiWrapper sealed
    {
    public:
        ApiWrapper()
        {
            if (!tracerInit())
            {
                throw gcnew System::Exception(gcnew System::String(
                    tracerErrorToString(tracerGetLastError())));
            }
        }

        ~ApiWrapper()
        {
            tracerShutdown();
        }
        !ApiWrapper()
        {
            tracerShutdown();
        }

        property int DllVersion
        {
        public:
            int get()
            {
                return tracerGetVersion();
            }
        private:
            void set(int value) {}
        }

        property System::String^ LastErrorMessage
        {
        public:
            System::String^ get()
            {
                return gcnew System::String(tracerErrorToString(tracerGetLastError()));
            }
        private:
            void set(System::String^ value) {}
        }

        System::UIntPtr AttachProcess(int pid)
        {
            return (System::UIntPtr)((void*)tracerAttachProcess(pid));
        }

        bool DetachProcess(System::UIntPtr ctx)
        {
            return tracerDetachProcess((TracerContext*)ctx.ToPointer());
        }

        property System::UIntPtr ProcessContext
        {
        public:
            System::UIntPtr get()
            {
                return (System::UIntPtr)((void*)tracerGetProcessContext());
            }
            void set(System::UIntPtr ctx)
            {
                tracerSetProcessContext((TracerContext*)ctx.ToPointer());
            }
        }

        System::UIntPtr GetContextForPid(int pid)
        {
            return (System::UIntPtr)((void*)tracerGetContextForPid(pid));
        }

        bool StartTrace(System::UIntPtr functionAddress)
        {
            return tracerStartTrace(functionAddress.ToPointer());
        }

        bool StartTrace(System::UIntPtr functionAddress, int threadId)
        {
            return tracerStartTrace(functionAddress.ToPointer(), threadId);
        }

        bool StartTrace(System::UIntPtr functionAddress, int threadId, int maxTraceDepth)
        {
            return tracerStartTrace(functionAddress.ToPointer(), threadId, maxTraceDepth);
        }

        bool StartTrace(System::UIntPtr functionAddress, int threadId, int maxTraceDepth, int lifetime)
        {
            return tracerStartTrace(functionAddress.ToPointer(), threadId, maxTraceDepth, lifetime);
        }

        bool StopTrace(System::UIntPtr functionAddress)
        {
            return tracerStopTrace(functionAddress.ToPointer());
        }

        bool StopTrace(System::UIntPtr functionAddress, int threadId)
        {
            return tracerStopTrace(functionAddress.ToPointer(), threadId);
        }

        array<TracedInstruction>^ FetchTraces()
        {
            System::Collections::ArrayList^ results = gcnew System::Collections::ArrayList();

            while (true)
            {
                TracerTracedInstruction traces[256];
                size_t numTraces = tracerFetchTraces(traces, 256);

                if (numTraces == 0)
                {
                    break;
                }

                for (size_t i = 0; i < numTraces; ++i)
                {
                    TracerTracedInstruction& inst = traces[i];
                    TracedInstruction tracedInst;

                    switch (inst.mType)
                    {
                    case eTracerInstructionTypeBranch:
                        tracedInst.Type = TracedInstructionType::Branch;
                        break;
                    case eTracerInstructionTypeCall:
                        tracedInst.Type = TracedInstructionType::Call;
                        break;
                    case eTracerInstructionTypeReturn:
                        tracedInst.Type = TracedInstructionType::Return;
                        break;
                    default:
                        tracedInst.Type = TracedInstructionType::Branch;
                    }

                    tracedInst.TraceId = inst.mTraceId;
                    tracedInst.ThreadId = inst.mThreadId;
                    tracedInst.CallDepth = inst.mCallDepth;

                    tracedInst.BranchSource = (System::UIntPtr)inst.mBranchSource;
                    tracedInst.BranchTarget = (System::UIntPtr)inst.mBranchTarget;

                    tracedInst.RegisterSet.Eax = (System::UIntPtr)inst.mRegisterSet.mEAX;
                    tracedInst.RegisterSet.Ebx = (System::UIntPtr)inst.mRegisterSet.mEBX;
                    tracedInst.RegisterSet.Ecx = (System::UIntPtr)inst.mRegisterSet.mECX;
                    tracedInst.RegisterSet.Edx = (System::UIntPtr)inst.mRegisterSet.mEDX;
                    tracedInst.RegisterSet.Esi = (System::UIntPtr)inst.mRegisterSet.mESI;
                    tracedInst.RegisterSet.Edi = (System::UIntPtr)inst.mRegisterSet.mEDI;
                    tracedInst.RegisterSet.Ebp = (System::UIntPtr)inst.mRegisterSet.mEBP;
                    tracedInst.RegisterSet.Esp = (System::UIntPtr)inst.mRegisterSet.mESP;

                    tracedInst.RegisterSet.SegGs = (System::UIntPtr)inst.mRegisterSet.mSegGS;
                    tracedInst.RegisterSet.SegFs = (System::UIntPtr)inst.mRegisterSet.mSegFS;
                    tracedInst.RegisterSet.SegEs = (System::UIntPtr)inst.mRegisterSet.mSegES;
                    tracedInst.RegisterSet.SegDs = (System::UIntPtr)inst.mRegisterSet.mSegDS;
                    tracedInst.RegisterSet.SegCs = (System::UIntPtr)inst.mRegisterSet.mSegCS;
                    tracedInst.RegisterSet.SegSs = (System::UIntPtr)inst.mRegisterSet.mSegSS;

                    results->Add(tracedInst);
                }
            }

            return (array<TracedInstruction>^)results->ToArray(TracedInstruction::typeid);
        }

        System::String^ DecodeAndFormatInstruction(System::UIntPtr address)
        {
            char buffer[256];
            if (tracerDecodeAndFormatInstruction((uintptr_t)address.ToUInt64(), buffer, sizeof(buffer)))
            {
                return gcnew System::String(buffer);
            }
            return nullptr;
        }

        System::UIntPtr GetSymbolAddress(System::String^ symbolName)
        {
            char* symbolNameStr = (char*)(void*)Marshal::StringToHGlobalAnsi(symbolName);
            if (!symbolNameStr) {
                return System::UIntPtr::Zero;
            }

            uintptr_t result = tracerGetSymbolAddressFromSymbolName(symbolNameStr);

            Marshal::FreeHGlobal((System::IntPtr)(void*)symbolNameStr);
            return (System::UIntPtr)result;
        }
    };
}
