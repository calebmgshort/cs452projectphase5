#include <usloss.h>
#include <usyscall.h>
#include <string.h>
#include "vm.h"
#include "phase5.h"
#include "phase5utility.h"
#include "providedPrototypes.h"

extern Process ProcTable[];
extern int vminitCalled;
extern int debugflag5;
extern int globalPages;

void p1_fork(int pid)
{
    // Don't run unluss VMInit has already been called
    if(!vminitCalled)
    {
        return;
    }

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_fork() called: pid = %d\n", pid);
    }

    // initialize the proc table for this process
    initProc(pid);
} /* p1_fork */

void p1_switch(int old, int new)
{
    // Don't run unluss VMInit has already been called
    if(!vminitCalled)
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
        dumpMappings();
    }

    // Unload all of the mappings from the old process
    for (int i = 0; i < globalPages; i++)
    {
        PTE *current = &oldProc->pageTable[i];
        if(current->frame != EMPTY)  // This PTE has a valid frame, so unmap it
        {
            int result = USLOSS_MmuUnmap(TAG, i);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("p1_switch(): Could not perform unmapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("p1_switch(): After unmapping %d.\n", i);
                dumpMappings();
            }
        }
    }

    if (DEBUG5 && debugflag5)
    {
        dumpMappings();
    }

    // Load all of the mappings for the new process
    for (int i = 0; i < globalPages; i++)
    {
        PTE *current = &newProc->pageTable[i];
        if (current->frame != EMPTY) // This PTE has a valid frame, so put it in the mmu
        {
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("Attempting to map frame %d to page %d for process %d\n", current->frame, i, new);
            }

            int result = USLOSS_MmuMap(TAG, i, current->frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("p1_switch(): Could not perform mapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }
        }
    }

    // Increment the number of switches
    vmStats.switches++;
} /* p1_switch */

void p1_quit(int pid)
{
    // Don't run unluss VMInit has already been called
    if(!vminitCalled)
    {
        return;
    }

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("p1_quit() called: pid = %d\n", pid);
    }

    // Free memory and set pointers to NULL
    Process *processPtr = getProc(pid);
    processPtr->pid = EMPTY;
    semfreeReal(processPtr->privateSem);
    memset(processPtr->pageTable, 0, globalPages * sizeof(PTE));
} /* p1_quit */
