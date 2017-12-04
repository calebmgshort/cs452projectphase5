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
extern int NumPages;
extern int NextCheckedFrame;
extern Frame *FrameTable;
extern int NumFrames;
extern void *vmRegion;

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

// Initialize the page table for the process with the given pid
void initPageTable(int pid)
{
    Process *proc = getProc(pid);
    for (int i = 0; i < NumPages; i++)
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
    for (int i = 0; i < NumPages; i++)
    {
        int frame;
        int protection;
        int result = USLOSS_MmuGetMap(TAG, i, &frame, &protection);
        if (result == USLOSS_MMU_ERR_NOMAP)
        {
            continue;
        }
        else if (result == USLOSS_MMU_ERR_OFF)
        {
            return;
        }
        else if (result == USLOSS_MMU_OK)
        {
            USLOSS_Console("\tPage %d mapped to frame %d\n", i, frame);
     
            if (FrameTable[frame].page != i)
            {
                USLOSS_Console("dumpMappings(): Found mapping inconsistent with the frame table.\n");
                for (int j = 0; j < NumFrames; j++)
                {
                    USLOSS_Console("\tFrame %d has page %d\n", j, FrameTable[j].page);
                }
                USLOSS_Halt(1);
            }
        }
        else
        {
            USLOSS_Console("dumpMappings(): Failed. Error code %d.\n", result);
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

void *page(int pageNum)
{
    return vmRegion + USLOSS_MmuPageSize() * pageNum;
}

void readPageFromDisk(char *buffer, int pid, int page)
{
    CheckMode();

    Process *proc = getProc(pid);
    int diskBlock = proc->pageTable[page].diskBlock;
    if (diskBlock == EMPTY)
    {
        USLOSS_Console("readPageFromDisk(): Trying to read page without a set diskBlock. pid %d page %d.\n", pid, page);
        USLOSS_Halt(1);
    }
    else if (proc->pageTable[page].state != ONDISK)
    {
        USLOSS_Console("readPageFromDisk(): Trying to read page that is not ONDISK. pid %d page %d.\n", pid, page);
        USLOSS_Halt(1);
    }

    // TODO also check the disk table for consistency

    int sectorsPerPage = USLOSS_MmuPageSize() / USLOSS_DISK_SECTOR_SIZE;
    int totalSectors = sectorsPerPage * diskBlock;
    int track = totalSectors / USLOSS_DISK_TRACK_SIZE;
    int sector = totalSectors % USLOSS_DISK_TRACK_SIZE;

    // Read into a buffer
    diskReadReal(1, track, sector, sectorsPerPage, buffer);
}

void writePageToDisk(char *buffer, int pid, int page)
{
    CheckMode();

    Process *proc = getProc(pid);
    int diskBlock = proc->pageTable[page].diskBlock;
    if (diskBlock == EMPTY)
    {
        USLOSS_Console("writePageToDisk(): Trying to write page without a set diskBlock. pid %d page %d.\n", pid, page);
        USLOSS_Halt(1);
    }
    else if (proc->pageTable[page].state != INMEM)
    {
        USLOSS_Console("writePageToDisk(): Trying to write page that is not INMEM. pid %d page %d.\n", pid, page);
        USLOSS_Halt(1);
    }

    // TODO also check the disk table for consistency

    int sectorsPerPage = USLOSS_MmuPageSize() / USLOSS_DISK_SECTOR_SIZE;
    int totalSectors = sectorsPerPage * diskBlock;
    int track = totalSectors / USLOSS_DISK_TRACK_SIZE;
    int sector = totalSectors / USLOSS_DISK_TRACK_SIZE;

    // Write the contents of the buffer
    diskWriteReal(1, track, sector, sectorsPerPage, buffer);
}
