/*
 *  File:  syscallHandlers.c
 *
 *  Description:  This file contains the syscall handlers for this phase
 *
 */

#include <usloss.h>
#include <usyscall.h>
#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include "syscallHandlers.h"
#include "phase5utility.h"
#include "vm.h"
#include "providedPrototypes.h"

extern void *vmInitReal(int, int, int, int);
extern void vmDestroyReal();

/*
 *  Syscall handler for VmInit
 */
void vmInit(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_VMINIT)
    {
        USLOSS_Console("vmInit(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    int mappings = (int) ((long) args->arg1);
    int pages = (int) ((long) args->arg2);
    int frames = (int) ((long) args->arg3);
    int pagers = (int) ((long) args->arg4);
    void *result = vmInitReal(mappings, pages, frames, pagers);
    if ((long) result <= 0)
    {
        args->arg1 = NULL;
        args->arg4 = result;
    }
    else
    {
        args->arg1 = result;
        args->arg4 = 0;
    }

    setToUserMode();
}

/*
 *  Syscall handler for VmDestroy
 */
void vmDestroy(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_VMDESTROY)
    {
        USLOSS_Console("vmDestroy(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    vmDestroyReal();
    setToUserMode();
}
