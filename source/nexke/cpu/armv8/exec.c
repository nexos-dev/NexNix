/*
    exec.c - contains exception handlers
    Copyright 2024 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <assert.h>
#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <string.h>

static PltHwIntCtrl_t* intCtrl = NULL;    // Interupt controller

// Exception name table
static const char* cpuExecNameTable[] = {[CPU_EC_UNKNOWN] = "unknown exception",
                                         [CPU_EC_TRAPPED_WF] = "trapped WFI/WFE",
                                         [CPU_EC_TRAPPED_MCR] = "trapped MCR/MRC from AArch32",
                                         [CPU_EC_TRAPPED_MCR2] = "trapped MCRR/MRRC from AArch32",
                                         [CPU_EC_TRAPPED_MCR3] = "trapped MCR/MRC from AArch32",
                                         [CPU_EC_TRAPPED_LDC] = "trapped LDC/STC from AArch32",
                                         [CPU_EC_TRAPPED_SIMD] = "trapped FP/SIMD",
                                         [CPU_EC_TRAPPED_LD64B] = "trapped LD/ST64B",
                                         [CPU_EC_TRAPPED_MRRC] = "trapped MRRC from AArch32",
                                         [CPU_EC_TRAPPED_BTI] = "branch target exception",
                                         [CPU_EC_AA32_SVC] = "SVC from AArch32",
                                         [CPU_EC_TRAPPED_MSRR] = "trapped MSRR",
                                         [CPU_EC_TRAPPED_SVC] = "SVC instruction",
                                         [CPU_EC_TRAPPED_MSR] = "trapped MSR/MRS",
                                         [CPU_EC_TRAPPED_SVE] = "trapped SVE",
                                         [CPU_EC_TRAPPED_TME] = "trapped TME",
                                         [CPU_EC_IA_LOW_EL] = "instruction abort",
                                         [CPU_EC_IA_CUR_EL] = "instruction abort",
                                         [CPU_EC_PC_ALIGN] = "PC alignment fault",
                                         [CPU_EC_DA_LOW_EL] = "data abort",
                                         [CPU_EC_DA_CUR_EL] = "data abort",
                                         [CPU_EC_SP_ALIGN] = "SP alignement fault",
                                         [CPU_EC_MOP_EXEC] = "memory operation exception",
                                         [CPU_EC_FPU_AA32] = "trapped FPU exception AArch32",
                                         [CPU_EC_FPU_AA64] = "trapped FPU exception",
                                         [CPU_EC_GCS] = "GCS exception",
                                         [CPU_EC_SERR] = "SError exception",
                                         [CPU_EC_BRK_LOW_EL] = "breakpoint exception",
                                         [CPU_EC_BRK_CUR_EL] = "breakpoint excpetion",
                                         [CPU_EC_SS_LOW_EL] = "single step exception",
                                         [CPU_EC_SS_CUR_EL] = "single step exception",
                                         [CPU_EC_WATCH_LOW_EL] = "watchpoint exception",
                                         [CPU_EC_WATCH_CUR_EL] = "watchpoint execption",
                                         [CPU_EC_BRKPT_AA32] = "BRKPT from AArch32",
                                         [CPU_EC_BRK] = "BRK instruction exception",
                                         [CPU_EC_PMU] = "PMU exception"};

// Returns interrupt number of currently executing interrupts based on ctx
int CpuGetIntNum (CpuIntContext_t* ctx)
{
    int code = 0;
    if (CPU_IS_EXEC (ctx->handler))
    {
        // Get ESR
        uint64_t esr = CpuReadSpr ("ESR_EL1");
        // Get exception code
        code = (esr >> CPU_ESR_EC_SHIFT) & CPU_ESR_EC_MASK;
    }
    else if (CPU_IS_IRQ (ctx->handler))
    {
        code = intCtrl->getVector (CpuGetCcb());
    }
    else
        assert (0);
    return code;
}

// Gets exception diagnostic info
void CpuGetExecInf (CpuExecInf_t* out, NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // Ensure exception is within bounds
    if (intObj->vector > 63)
        PltBadTrap (ctx, "invalid exception");    // Very odd indeed
    out->name = cpuExecNameTable[intObj->vector];
}

// Data abort handler
bool CpuDataAbort (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // Get ISS
    uint32_t iss = CpuReadSpr ("ESR_EL1") & CPU_ESR_ISS_MASK;
    int dfsc = iss & CPU_DA_DFSC_MASK;
    if (CPU_IS_AS_FAULT (dfsc))
        return false;    // No way to handle an address fault
    else if (CPU_IS_TRAN_FAULT (dfsc))
    {
        int errCode = (iss & CPU_DA_WNR) ? MUL_PAGE_RW : 0;
        errCode |= MUL_PAGE_P;
        uintptr_t addr = CpuReadSpr ("FAR_EL1");
        return MmPageFault (addr, errCode);
    }
    else if (CPU_IS_PERM_FAULT (dfsc))
    {
        int errCode = (iss & CPU_DA_WNR) ? MUL_PAGE_RW : 0;
        uintptr_t addr = CpuReadSpr ("FAR_EL1");
        return MmPageFault (addr, errCode);
    }
    else if (CPU_IS_AF_FAULT (dfsc))
    {
        uintptr_t addr = CpuReadSpr ("FAR_EL1");
        MmMulSetAttr (MmGetCurrentSpace(), addr, MUL_ATTR_ACCESS, true);
        return true;
    }
    else
        return false;
}

// Instruction abort handler
bool CpuInsnAbort (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // Get ISS
    uint32_t iss = CpuReadSpr ("ESR_EL1") & CPU_ESR_ISS_MASK;
    int ifsc = iss & CPU_IA_IFSC_MASK;
    if (CPU_IS_AS_FAULT (ifsc))
        return false;    // No way to handle an address fault
    else if (CPU_IS_TRAN_FAULT (ifsc))
    {
        int errCode = MUL_PAGE_X;
        uintptr_t addr = CpuReadSpr ("FAR_EL1");
        return MmPageFault (addr, errCode);
    }
    else if (CPU_IS_PERM_FAULT (ifsc))
    {
        int errCode = MUL_PAGE_P | MUL_PAGE_X;
        uintptr_t addr = CpuReadSpr ("FAR_EL1");
        return MmPageFault (addr, errCode);
    }
    else if (CPU_IS_AF_FAULT (ifsc))
    {
        uintptr_t addr = CpuReadSpr ("FAR_EL1");
        MmMulSetAttr (MmGetCurrentSpace(), addr, MUL_ATTR_ACCESS, true);
        return true;
    }
    else
        return false;
}

// Registers exception handlers
void CpuRegisterExecs()
{
    for (int i = 0; i < CPU_EC_NUM; ++i)
    {
        if (i == CPU_EC_DA_LOW_EL || i == CPU_EC_DA_CUR_EL)
            PltInstallExec (i, CpuDataAbort);
        else if (i == CPU_EC_IA_LOW_EL || i == CPU_EC_IA_CUR_EL)
            PltInstallExec (i, CpuInsnAbort);
        PltInstallExec (i, NULL);
    }
    intCtrl = PltGetPlatform()->intCtrl;
}
