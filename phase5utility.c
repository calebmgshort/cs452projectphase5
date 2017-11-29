#include <usloss.h>
#include <usyscall.h>
#include <assert.h>
#include <stdlib.h>

#include "phase2.h"
#include "phase5.h"
#include "syscallHandlers.h"
#include "phase5utility.h"
#include "vm.h"

extern Process ProcTable[];

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

void initProc(int pid, int pages)
{
    Process proc = ProcTable[pid % MAXPROC];
    proc.numPages = pages;
    proc.pageTable = NULL;    // TODO: What should I set the pageTable to?
    proc.privateMboxID = MboxCreate(1, 0);
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

extern void diskSizeReal(int, int *, int *, int *);
void initVmStats(VmStats *vmStats, int pages, int frames)
{
    vmStatsMutex = createMutex();
    CheckMode();

    vmStats->pages = pages;
    vmStats->frames = frames;
    int sector;
    int track;
    int disk;
    diskSizeReal(1, &sector, &track, &disk);
    int diskSize = disk * track * sector;
    vmStats->diskBlocks = diskSize/USLOSS_MmuPageSize();
    vmStats->freeFrames = frames;
    vmStats->freeDiskBlocks = vmStats->diskBlocks;
    vmStats->switches = 0;
    vmStats->faults = 0;
    vmStats->new = 0;
    vmStats->pageIns = 0;
    vmStats->pageOuts = 0;
    vmStats->replaced = 0;
}
