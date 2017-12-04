#include <usloss.h>
#include <usyscall.h>
#include <string.h>
#include "phase5.h"
#include "vm.h"
#include "phase5utility.h"
#include "providedPrototypes.h"

extern int VMInitialized;
extern int debugflag5;
extern int NumPages;
extern Frame *FrameTable;
extern int NumFrames;

void p1_fork(int pid)
{
    // Don't run unluss VMInit has already been called
    if(!VMInitialized)
    {
        return;
    }

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_fork() called: pid = %d\n", pid);
    }

    // initialize the proc table for this process
    Process *proc = getProc(pid);
    proc->pid = pid;
    proc->privateSem = semcreateReal(0);
    if (proc->privateSem < 0)
    {
        USLOSS_Console("p1_fork(): Could not create private semaphore.\n");
        USLOSS_Halt(1);
    }
    initPageTable(pid);
} /* p1_fork */

static void unloadMappings(const char *caller, int pid)
{
    Process *proc = getProc(pid);
    for (int i = 0; i < NumPages; i++)
    {
        PTE *pte = proc->pageTable + i;

        // Unmap if the PTE has a valid frame
        if (pte->frame != EMPTY)
        {
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("%s(): Attempting to unmap page %d from frame %d for process %d\n", caller, i, pte->frame, pid);
            }

            if (FrameTable[pte->frame].page != i)
            {
                USLOSS_Console("%s(): Found inconsistent data in frame table.\n", caller);
                USLOSS_Halt(1);
            }

            if (pte->state != INMEM)
            {
                USLOSS_Console("%s(): Inconsistent page state for (old) page %d.\n", caller, i);
                USLOSS_Halt(1);
            }

            int result = USLOSS_MmuUnmap(TAG, i);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("%s(): Could not perform unmapping. Error code %d.\n", caller, result);
                USLOSS_Halt(1);
            }

            FrameTable[pte->frame].page = EMPTY;
        }
    }
}

void p1_switch(int old, int new)
{
    // Don't run unluss VMInit has already been called
    if(!VMInitialized)
    {
        return;
    }

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_switch() called: old = %d, new = %d\n", old, new);
    }

    Process *newProc = getProc(new);

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_switch(): Initial mappings for process %d: \n", old);
        dumpMappings();
    }

    // Unload all of the mappings from the old process
    unloadMappings("p1_switch", old);

    // Ensure that all the frames are empty now
    for (int i = 0; i < NumFrames; i++)
    {
        if (FrameTable[i].page != EMPTY)
        {
            USLOSS_Console("p1_switch(): Unclean intermediate frame table.\n");
            for (int j = 0; j < NumFrames; j++)
            {
                USLOSS_Console("\tFrame %d contains page %d\n", j, FrameTable[j].page);
            }
            USLOSS_Halt(1);
        }
    }

    // Load all of the mappings for the new process
    for (int i = 0; i < NumPages; i++)
    {
        PTE *current = &newProc->pageTable[i];
        // Load the mapping if the PTE indicates the page should be in memory
        if (current->frame != EMPTY)
        {
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("p1_switch(): Attempting to map frame %d to page %d for process %d\n", current->frame, i, new);
            }

            if (current->state != INMEM)
            {
                USLOSS_Console("p1_switch(): Inconsistent page state for (new) page %d.\n", i);
                USLOSS_Halt(1);
            }

            int result = USLOSS_MmuMap(TAG, i, current->frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("p1_switch(): Could not perform mapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }

            FrameTable[current->frame].page = i;
        }
    }

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_switch(): Mappings for process %d: \n", new);
        dumpMappings();
    }

    // Increment the number of switches
    vmStats.switches++;
} /* p1_switch */

void p1_quit(int pid)
{
    // Don't run unluss VMInit has already been called
    if(!VMInitialized)
    {
        return;
    }

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_quit() called: pid = %d\n", pid);
    }
    
    // Unload all mappings without regards to page table
    unloadMappings("p1_quit", pid);
    
    // Clean up the proc table entry for this process.
    Process *processPtr = getProc(pid);
    if (processPtr->pid == EMPTY)
    {
        // This is a pre vmInit proc
        return;
    }
    processPtr->pid = EMPTY;
    int result = semfreeReal(processPtr->privateSem);
    if (result != 0)
    {
        USLOSS_Console("p1_quit(): Error in freeing private semaphore.\n");
        USLOSS_Halt(1);
    }
    initPageTable(pid);
} /* p1_quit */
