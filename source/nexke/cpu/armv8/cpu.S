// cpu.S - contains CPU asm code
// Copyright 2024 The NexNix Project
//
// Licensed under the Apache License, Version 2.0 (the "License")
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


.global CpuCrash

CpuCrash:
    mov x3, #0xF
    msr daif, x3
    wfi
    b CpuCrash

.global MmMulFlush

MmMulFlush:
    lsr x0, x0, #12
    dsb ishst
    tlbi vaae1is, x0
    ret

.global MmMulFlushTlb

MmMulFlushTlb:
    dsb ishst
    tlbi vmalle1
    dsb ish
    isb
    ret

// Performs a context switch
// Very critical code path
.global CpuSwitchContext
CpuSwitchContext:

// Performs a resverse bit scan
// Used for priority checking
.global CpuScanPriority
CpuScanPriority:
    clz w8, w0
    mov w9, #31
    sub w0, w9, w8
    ret

