/*
    main.c - contains entry point
    Copyright 2022, 2023 The NexNix Project

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
#include <nexboot/detect.h>
#include <nexboot/driver.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/drivers/time.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// The main entry point into nexboot
void NbMain (NbloadDetect_t* nbDetect)
{
    // So, we are loaded by nbload, and all it has given us is the nbdetect
    // structure. It's our job to create a usable environment.

    // Initialize logging
    NbLogInit();
    // Initialize memory allocation
    NbMemInit();
    // Initialize object database
    NbObjInitDb();
    // Start phase 1 drivers
    if (!NbStartPhase1Drvs())
    {
        NbLogMessageEarly ("Error: Unable to start phase 1 drivers",
                           NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    // Detect hardware devices and add them to object database
    if (!NbFwDetectHw (nbDetect))
    {
        NbLogMessageEarly ("Error: Unable to detect hardware devices",
                           NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    // Start phase 2 of drivers
    if (!NbStartPhase2Drvs())
    {
        NbLogMessageEarly ("Error: Unable to start phase 2 drivers",
                           NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    NbObject_t* term1 = NbObjFind ("/Devices/Terminal0");
    NbObject_t* term2 = NbObjFind ("/Devices/Terminal1");
    for (int i = 0; i < 20; ++i)
        NbObjCallSvc (term2, NB_TERMINAL_WRITE, "test string\n");
    NbObjCallSvc (term2, NB_TERMINAL_WRITE, "\e[44;37mtest \e[0m\e[10D tring");
    char buf[32];
    NbTermRead_t termRead = {0};
    termRead.buf = buf;
    termRead.bufSz = 32;
    while (1)
    {
        memset (buf, 0, 32);
        NbObjCallSvc (term2, NB_TERMINAL_READ, &termRead);
        NbObjCallSvc (term1, NB_TERMINAL_WRITE, (void*) buf);
    }
    for (;;)
        ;
}
