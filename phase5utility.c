#include <usloss.h>
#include <usyscall.h>
#include <assert.h>
#include <stdlib.h>

#include "phase2.h"
#include "phase5.h"
#include "syscallHandlers.h"
#include "phase5utility.h"
#include "vm.h"
#include "providedPrototypes.h"

extern Process ProcTable[];
extern int globalPages;
extern int NextCheckedFrame;

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

// Initialize the given process
void initProc(int pid)
{
    Process *proc = getProc(pid);
    proc->pid = pid;
    initPageTable(pid);
    proc->numPages = 0;
    proc->privateSem = semcreateReal(0);
    if (proc->privateSem < 0)
    {
        USLOSS_Console("initProc(%d): Could not create private semaphore", pid);
        USLOSS_Halt(1);
    }
}

// Initialize the page table for the process with the given pid
void initPageTable(int pid)
{
    Process *proc = getProc(pid);
    for (int i = 0; i < globalPages; i++)
    {
        proc->pageTable[i].state = UNUSED;
        proc->pageTable[i].frame = EMPTY;
        proc->pageTable[i].diskBlock = EMPTY;
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

Process *getProc(int pid)
{
    return &ProcTable[pid % MAXPROC];
}

void semPProc()
{
    CheckMode();
    sempReal(getProc(getpid())->privateSem);
}

void semVProc(int pid)
{
    CheckMode();
    semvReal(getProc(pid)->privateSem);
}

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

void enableInterrupts()
{
    int result = USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
    if (result != USLOSS_DEV_OK)
    {
        USLOSS_Console("ClockDriver(): Bug in enable interrupts.\n");
        USLOSS_Halt(1);
    }
}

void dumpMappings()
{
    USLOSS_Console("dumpMappings(): called\n");
    for (int i = 0; i < globalPages; i++)
    {
        int frame;
        int protection;
        int result = USLOSS_MmuGetMap(TAG, i, &frame, &protection);
        if (result != USLOSS_MMU_ERR_NOMAP)
        {
            USLOSS_Console("\tPage %d mapped to frame %d\n", i, frame);
        }
    }
}

int getNextFrame()
{
    // Search for a free frame
    for (int i = 0; i < NumFrames; i++)
    {
        if (FrameTable[i].page == EMPTY)
        {
            return i;
        }
    }

    // If there isn't one then use clock algorithm to replace a page
    for (int i = 0; i < NumFrames + 1; i++)
    {
        int index = (NextCheckedFrame + i) % NumFrames;
        int access;
        int result = USLOSS_MmuGetAccess(index, &access);
        if (result != USLOSS_MMU_OK)
        {
            USLOSS_Console("Pager(): Could not read frame access bits.\n");
            USLOSS_Halt(1);
        }

        if (access & USLOSS_MMU_REF)
        {
            result = USLOSS_MmuSetAccess(index, access & ~USLOSS_MMU_REF);

            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("Pager(): Could not set frame access bits.\n");
                USLOSS_Halt(1);
            }
        }
        else
        {
            NextCheckedFrame = (index + 1) % NumFrames;
            return index;
        }
    }

    USLOSS_Console("getNextFrame(): Error: inconsistent data.");
    USLOSS_Halt(1);

    return -1;
}
