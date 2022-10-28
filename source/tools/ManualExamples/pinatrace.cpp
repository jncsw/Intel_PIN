/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 *  This file contains an ISA-portable PIN tool for tracing memory accesses.
 */


#include <chrono>
#include <iostream>
#include <sys/time.h>
#include <ctime>

#include <stdio.h>
#include "pin.H"
FILE* trace;

unsigned long lo = 1073741824;
unsigned long hi = lo*3-1;

bool inRange(VOID* pt) {
    // if ((unsigned long)pt < lo || (unsigned long)pt > hi) return false;
    return true;
}

// Print a memory read record
VOID RecordMemRead(VOID* ip, VOID* addr) {
     if (inRange(addr)){
        struct timeval time_now{};
        gettimeofday(&time_now, nullptr);
        time_t msecs_time = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
        fprintf(trace, "[%ld] R %p\n", //123LL, addr); 
            msecs_time,
            addr); 
     } 
}

// Print a memory write record
VOID RecordMemWrite(VOID* ip, VOID* addr) { 
    if (inRange(addr)){
        
        struct timeval time_now{};
        gettimeofday(&time_now, nullptr);
        time_t msecs_time = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
        fprintf(trace, "[%ld] W %p\n", //123LL, addr); 
            msecs_time,
            addr); 

        // fprintf(trace, "[%lld] W %p\n", //123LL, addr); 
			// std::chrono::duration_cast<std::chrono::milliseconds>
            //   (std::chrono::high_resolution_clock::now().time_since_epoch()).count(),
            // addr); 
     }
}

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID* v)
{
    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP
    // prefixed instructions appear as predicated instructions in Pin.
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYOP_EA, memOp,
                                     IARG_END);
        }
        // Note that in some architectures a single memory operand can be
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite, IARG_INST_PTR, IARG_MEMORYOP_EA, memOp,
                                     IARG_END);
        }
    }
}

VOID Fini(INT32 code, VOID* v)
{
    fprintf(trace, "#eof\n");
    fclose(trace);
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("This Pintool prints a trace of memory addresses\n" + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv)) return Usage();

    trace = fopen("pinatrace.out", "w");

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
