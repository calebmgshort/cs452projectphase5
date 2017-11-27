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
    // TODO output
    setToUserMode();
}

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

void mboxCreate(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_MBOXCREATE)
    {
        USLOSS_Console("mboxCreate(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    int numslots = (int) ((long) args->arg1);
    int slotsize = (int) ((long) args->arg2);
    int result = MboxCreate(numslots, slotsize);
    if (result < 0)
    {
        args->arg1 = (void *) -1;
        args->arg4 = (void *) -1;
    }
    else
    {
        args->arg1 = (void *) ((long) result);
        args->arg4 = (void *) 0;
    }
    setToUserMode();
}

void mboxRelease(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_MBOXRELEASE)
    {
        USLOSS_Console("mboxRelease(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    int mboxID = (int) ((long) args->arg1);
    int result = MboxRelease(mboxID);
    if (result < 0)
    {
        args->arg4 = (void *) -1;
    }
    else
    {
        args->arg4 = (void *) 0;
    }
    setToUserMode();
}

void mboxSend(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_MBOXSEND)
    {
        USLOSS_Console("mboxSend(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    int mboxID = (int) ((long) args->arg1);
    void *msgPtr = args->arg2;
    int msgSize = (int) ((long) args->arg3);
    int result = MboxSend(mboxID, msgPtr, msgSize);
    if (result < 0)
    {
        args->arg4 = (void *) -1;
    }
    else
    {
        args->arg4 = (void *) 0;
    }
    setToUserMode();
}

void mboxCondSend(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_MBOXCONDSEND)
    {
        USLOSS_Console("mboxCondSend(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    int mboxID = (int) ((long) args->arg1);
    void *msgPtr = args->arg2;
    int msgSize = (int) ((long) args->arg3);
    int result = MboxCondSend(mboxID, msgPtr, msgSize);
    if (result == -2)
    {
        args->arg4 = (void *) 1;
    }
    else if (result < 0)
    {
        args->arg4 = (void *) -1;
    }
    else
    {
        args->arg4 = (void *) 0;
    }
    setToUserMode();
}

void mboxReceive(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_MBOXRECEIVE)
    {
        USLOSS_Console("mboxReceive(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    int mboxID = (int) ((long) args->arg1);
    void *msgPtr = (void *) args->arg2;
    int msgSize = (int) ((long) args->arg3);
    int result = MboxReceive(mboxID, msgPtr, msgSize);
    if (result < 0)
    {
        args->arg4 = (void *) -1;
    }
    else
    {
        args->arg4 = (void *) 0;
    }
    setToUserMode();
}

void mboxCondReceive(USLOSS_Sysargs *args)
{
    CheckMode();
    if (args->number != SYS_MBOXCONDRECEIVE)
    {
        USLOSS_Console("mboxCondReceive(): Called with wrong syscall number.\n");
        USLOSS_Halt(1);
    }
    int mboxID = (int) ((long) args->arg1);
    void *msgPtr = args->arg2;
    int msgSize = (int) ((long) args->arg3);
    int result = MboxCondReceive(mboxID, msgPtr, msgSize);
    if (result == -2)
    {
        args->arg4 = (void *) 1;
    }
    else if (result < 0)
    {
        args->arg4 = (void *) -1;
    }
    else
    {
        args->arg4 = (void *) 0;
    }
    setToUserMode();
}
