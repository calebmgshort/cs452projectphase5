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

    Process *oldProc = getProc(old);
    Process *newProc = getProc(new);

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_switch(): Initial mappings for process %d: \n", old);
        dumpMappings();
    }

    // Unload all of the mappings from the old process
    for (int i = 0; i < NumPages; i++)
    {
        PTE *current = &oldProc->pageTable[i];
        // Unmap if the PTE has a valid frame
        if (current->frame != EMPTY)
        {
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("p1_switch(): Attempting to unmap page %d from frame %d for process %d\n", i, current->frame, old);
            }

            if (FrameTable[current->frame].page != i)
            {
                USLOSS_Console("p1_switch(): Found inconsistent data in frame table.\n");
                USLOSS_Halt(1);
            }

            if (current->state != INMEM)
            {
                USLOSS_Console("p1_switch(): Inconsistent page state for (old) page %d.\n", i);
                USLOSS_Halt(1);
            }

            int result = USLOSS_MmuUnmap(TAG, i);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("p1_switch(): Could not perform unmapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }

            current->frame = EMPTY;
            current->state = ONDISK;
            // TODO write contents of page to disk if they are unclean
            FrameTable[current->frame].page = EMPTY;
        }
    }

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

            if (current->state != ONDISK)
            {
                USLOSS_Console("p1_switch(): Inconsistent page state for (new) page %d.\n", i);
            }

            int result = USLOSS_MmuMap(TAG, i, current->frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("p1_switch(): Could not perform mapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }

            current->state = INMEM;
            FrameTable[current->frame].page = i;
            // TODO load disk contents if necessary
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

    // TODO when a process quits, what happens to its mappings?
    // Check that switch is called or handle that here.
    // If switch is called, will the page table for this proc cause problems?

    // Clean up the proc table entry for this process.
    Process *processPtr = getProc(pid);
    processPtr->pid = EMPTY;
    int result = semfreeReal(processPtr->privateSem);
    if (result != 0)
    {
        USLOSS_Console("p1_quit(): Error in freeing private semaphore.\n");
        USLOSS_Halt(1);
    }
    initPageTable(pid);
} /* p1_quit */
