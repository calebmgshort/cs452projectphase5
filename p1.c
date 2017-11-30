#include <usloss.h>
#include <usyscall.h>
#include "vm.h"
#include "phase5.h"

extern Process ProcTable[];
extern int vminitCalled;
extern int debugflag5;

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
    Process *processPtr = &ProcTable[pid];
    processPtr->pageTable = NULL;
    processPtr->numPages = 0;
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

    Process *oldProc = &ProcTable[old];
    Process *newProc = &ProcTable[new];
    PTE *current;
    int count;

    // Unload all of the mappings from the old process
    current = oldProc->pageTable;
    count = 0;
    while(current != NULL)
    {
        if(current->frame != -1)  // This PTE has a valid frame, so unmap it
        {
            int result = USLOSS_MmuUnmap(TAG, count);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("p1_switch(): Could not perform unmapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }
        }
        count++;
    }

    // Load all of the mappings for the new process
    current = newProc->pageTable;
    count = 0;
    while(current != NULL)
    {
        if(current->frame != -1)  // This PTE has a valid frame, so unmap it
        {
            int result = USLOSS_MmuMap(TAG, count, current->frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("p1_switch(): Could not perform unmapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }
        }
        count++;
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
    Process *processPtr = &ProcTable[pid];
    PTE *current = processPtr->pageTable;
    PTE *last = NULL;
    while(current != NULL)
    {
        last = current;
        current = current->next;
        free(last);
        last = NULL;
    }
    processPtr->pageTable = NULL;
} /* p1_quit */
