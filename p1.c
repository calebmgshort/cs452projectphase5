/*
 *  File:  p1.c
 *
 *  Description:  This file contains the phase5 functions called by phase1
 *
 */

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

/*
 *  The code that phase 1 should call when a new process is forked.
 *  Initializes the phase 5 process table for this process
 */
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

/*
 *  Unload the mappings for the process with the given pid
 */
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
                USLOSS_Console("%s(): Frame table has wrong page for frame %d.\n", caller, pte->frame);
                USLOSS_Halt(1);
            }
            if (FrameTable[pte->frame].pid != pid)
            {
                USLOSS_Console("%s(): Frame table has wrong pid for frame %d.\n", caller, pte->frame);
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
        }
    }
}

/*
 *  The code that phase 1 should call when a process switch occurs
 *  Changes out the mappings based on the page tables of the old and new procs
 */
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

            // Check the frame table for consistency
            if (FrameTable[current->frame].page != i)
            {
                USLOSS_Console("p1_switch(): Frame table has invalid page for frame %d\n", current->frame);
                USLOSS_Halt(1);
            }
            if (FrameTable[current->frame].pid != new)
            {
                USLOSS_Console("p1_switch(): Frame table has invalid pid for frame %d\n", current->frame);
                USLOSS_Halt(1);
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

/*
 *  The code that phase 1 should call when a process is quit
 *  Cleans up the phase 5 proc table proc table for the given proc
 */
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

    // Unload our mappings
    unloadMappings("p1_quit", pid);

    // Clear out our frames
    for (int i = 0; i < NumFrames; i++)
    {
        if (FrameTable[i].pid == pid)
        {
            FrameTable[i].pid = EMPTY;
            FrameTable[i].page = EMPTY;
        }
    }

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
