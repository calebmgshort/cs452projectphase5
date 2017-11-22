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

/*
 * Sets the current process into user mode. Requires the process to currently
 * be in kernel mode. Also enables interrupts.
 */
void setToUserMode()
{
    CheckMode();
    unsigned int psr = USLOSS_PsrGet();
    unsigned int newpsr = (psr & ~USLOSS_PSR_CURRENT_MODE) | USLOSS_PSR_CURRENT_INT;
    int result = USLOSS_PsrSet(newpsr);
    if (result != USLOSS_DEV_OK)
    {
        USLOSS_Console("setToUserMode(): Bug in psr set.  Halting...\n");
        USLOSS_Halt(1);
    }
}

int createMutex()
{
    CheckMode();
    return MboxCreate(1, 0);
}

void lockMutex(int mbox)
{
    CheckMode();
    MboxSend(mbox, NULL, 0);
}

void unlockMutex(int mbox)
{
    CheckMode();
    MboxReceive(mbox, NULL, 0);
}
