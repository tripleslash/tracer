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
        System::IntPtr Eax;
        System::IntPtr Ebx;
        System::IntPtr Ecx;
        System::IntPtr Edx;
        System::IntPtr Esi;
        System::IntPtr Edi;
        System::IntPtr Ebp;
        System::IntPtr Esp;

        System::IntPtr SegGs;
        System::IntPtr SegFs;
        System::IntPtr SegEs;
        System::IntPtr SegDs;
        System::IntPtr SegCs;
        System::IntPtr SegSs;
    };

    public value struct TracedInstruction
    {
        TracedInstructionType Type;

        int TraceId;
        int ThreadId;
        int CallDepth;

        System::IntPtr BranchSource;
        System::IntPtr BranchTarget;

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

        System::IntPtr AttachProcess(int pid)
        {
            return (System::IntPtr)((void*)tracerAttachProcess(pid));
        }

        bool DetachProcess(System::IntPtr ctx)
        {
            return tracerDetachProcess((TracerContext*)ctx.ToPointer());
        }

        property System::IntPtr ProcessContext
        {
        public:
            System::IntPtr get()
            {
                return (System::IntPtr)((void*)tracerGetProcessContext());
            }
            void set(System::IntPtr ctx)
            {
                tracerSetProcessContext((TracerContext*)ctx.ToPointer());
            }
        }

        System::IntPtr GetContextForPid(int pid)
        {
            return (System::IntPtr)((void*)tracerGetContextForPid(pid));
        }

        bool StartTrace(System::IntPtr functionAddress)
        {
            return tracerStartTrace(functionAddress.ToPointer());
        }

        bool StartTrace(System::IntPtr functionAddress, int threadId)
        {
            return tracerStartTrace(functionAddress.ToPointer(), threadId);
        }

        bool StartTrace(System::IntPtr functionAddress, int threadId, int maxTraceDepth)
        {
            return tracerStartTrace(functionAddress.ToPointer(), threadId, maxTraceDepth);
        }

        bool StartTrace(System::IntPtr functionAddress, int threadId, int maxTraceDepth, int lifetime)
        {
            return tracerStartTrace(functionAddress.ToPointer(), threadId, maxTraceDepth, lifetime);
        }

        bool StopTrace(System::IntPtr functionAddress)
        {
            return tracerStopTrace(functionAddress.ToPointer());
        }

        bool StopTrace(System::IntPtr functionAddress, int threadId)
        {
            return tracerStopTrace(functionAddress.ToPointer(), threadId);
        }

        array<TracedInstruction>^ FetchTraces()
        {
            System::Collections::ArrayList^ results = gcnew System::Collections::ArrayList();

            while (true)
            {
                TracerTracedInstruction traces[64];
                size_t numTraces = tracerFetchTraces(traces, 64);

                if (numTraces == 0 || numTraces >= 64)
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

                    tracedInst.BranchSource = (System::IntPtr)((long long)inst.mBranchSource);
                    tracedInst.BranchTarget = (System::IntPtr)((long long)inst.mBranchTarget);

                    tracedInst.RegisterSet.Eax = (System::IntPtr)((long long)inst.mRegisterSet.mEAX);
                    tracedInst.RegisterSet.Ebx = (System::IntPtr)((long long)inst.mRegisterSet.mEBX);
                    tracedInst.RegisterSet.Ecx = (System::IntPtr)((long long)inst.mRegisterSet.mECX);
                    tracedInst.RegisterSet.Edx = (System::IntPtr)((long long)inst.mRegisterSet.mEDX);
                    tracedInst.RegisterSet.Esi = (System::IntPtr)((long long)inst.mRegisterSet.mESI);
                    tracedInst.RegisterSet.Edi = (System::IntPtr)((long long)inst.mRegisterSet.mEDI);
                    tracedInst.RegisterSet.Ebp = (System::IntPtr)((long long)inst.mRegisterSet.mEBP);
                    tracedInst.RegisterSet.Esp = (System::IntPtr)((long long)inst.mRegisterSet.mESP);

                    tracedInst.RegisterSet.SegGs = (System::IntPtr)((long long)inst.mRegisterSet.mSegGS);
                    tracedInst.RegisterSet.SegFs = (System::IntPtr)((long long)inst.mRegisterSet.mSegFS);
                    tracedInst.RegisterSet.SegEs = (System::IntPtr)((long long)inst.mRegisterSet.mSegES);
                    tracedInst.RegisterSet.SegDs = (System::IntPtr)((long long)inst.mRegisterSet.mSegDS);
                    tracedInst.RegisterSet.SegCs = (System::IntPtr)((long long)inst.mRegisterSet.mSegCS);
                    tracedInst.RegisterSet.SegSs = (System::IntPtr)((long long)inst.mRegisterSet.mSegSS);

                    results->Add(tracedInst);
                }
            }

            return (array<TracedInstruction>^)results->ToArray(TracedInstruction::typeid);
        }
	};
}
