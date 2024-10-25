# crt0.S - contains program entry point
# Copyright 2024 The NexNix Project
#
# Licensed under the Apache License, Version 2.0 (the "License")#
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.global _start
.extern __libc_main
.extern _exit

_start:
    # Setup stack frame
    mov x29, 0
    stp x29, x30, [sp,#-16]!
    bl __libc_main
    bl _exit
