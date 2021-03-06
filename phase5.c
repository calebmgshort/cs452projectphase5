/*
 *  File:  phase5.c
 *
 *  Description:  This file contains the functions that maintain virtual memory,
 *                the fault handler, and the pager
 *
 */
#include <usloss.h>
#include <usyscall.h>
#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include <libuser.h>
#include <vm.h>
#include <string.h>

#include "syscallHandlers.h"
#include "phase5utility.h"
#include "providedPrototypes.h"

// Debugging flag
int debugflag5 = 0;

// Process info
Process ProcTable[MAXPROC];

/*
 * faults[i] stores info about the current page fault for the process stored in
 * ProcTable[i]. Only the Pagers and the process who "owns" faults[i] may
 * access it. Their interaction is already managed, so no mutex is necessary.
 */
FaultMsg faults[MAXPROC];

// Vm Stats
VmStats vmStats;
int vmStatsMutex;

// Pager info
int NumPagers;
int PagerPIDs[MAXPAGERS];
int FaultsMbox;
int PagerKillSem;
int NextBlock = 0;

// Start of the Vm Region
void *vmRegion;

// Frame table
Frame *FrameTable;
int NextCheckedFrame = 0;
int FramesMutex;

// Global Mmu info
int VMInitialized = FALSE;
int NumPages = 0;
int NumFrames = 0;

static void FaultHandler(int, void *);
static int Pager(char *);

extern int start5(char *);

/*
 *----------------------------------------------------------------------
 *
 * start4 --
 *
 * Initializes the VM system call handlers.
 *
 * Results:
 *      MMU return status
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
int start4(char *arg)
{
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("start4(): called.\n");
    }

    /* to get user-process access to mailbox functions */
    systemCallVec[SYS_MBOXCREATE]      = mbox_create;
    systemCallVec[SYS_MBOXRELEASE]     = mbox_release;
    systemCallVec[SYS_MBOXSEND]        = mbox_send;
    systemCallVec[SYS_MBOXRECEIVE]     = mbox_receive;
    systemCallVec[SYS_MBOXCONDSEND]    = mbox_condsend;
    systemCallVec[SYS_MBOXCONDRECEIVE] = mbox_condreceive;

    /* user-process access to VM functions */
    systemCallVec[SYS_VMINIT]    = vmInit;
    systemCallVec[SYS_VMDESTROY] = vmDestroy;

    int pid;
    int result = Spawn("Start5", start5, NULL, 8 * USLOSS_MIN_STACK, 2, &pid);
    if (result != 0)
    {
        USLOSS_Console("start4(): Error spawning start5\n");
        Terminate(1);
    }
    int status;
    result = Wait(&pid, &status);
    if (result != 0)
    {
        USLOSS_Console("start4(): Error waiting for start5\n");
        Terminate(1);
    }
    Terminate(0);
    return 0;
} /* start4 */

/*
 *----------------------------------------------------------------------
 *
 * vmInitReal --
 *
 * Called by vmInit.
 * Initializes the VM system by configuring the MMU and setting
 * up the page tables.
 *
 * Results:
 *      Address of the VM region.
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
void *vmInitReal(int mappings, int pages, int frames, int pagers)
{
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("vmInitReal(): called.\n");
    }

    CheckMode();

    // Check args
    if (mappings < 0 || pages < 0 || frames < 0 || pagers < 0)
    {
        return (void *) -1;
    }
    if (mappings != pages)
    {
        return (void *) -1;
    }
    if (pagers > MAXPAGERS)
    {
        return (void *) -1;
    }

    // Initialize the proc table
    for (int i = 0; i < MAXPROC; i++)
    {
        getProc(i)->pid = EMPTY;
    }

    // Init the Mmu
    int status = USLOSS_MmuInit(mappings, pages, frames, USLOSS_MMU_MODE_TLB);
    if (status != USLOSS_MMU_OK)
    {
       USLOSS_Console("vmInitReal(): couldn't initialize MMU, status %d\n", status);
       USLOSS_Halt(1);
    }
    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

    // Initialize page tables.
    NumPages = pages;
    for (int i = 0; i < MAXPROC; i++)
    {
        Process *proc = getProc(i);
        proc->pageTable = malloc(pages * sizeof(PTE));
        if (proc->pageTable == NULL)
        {
            USLOSS_Console("vmInitReal(): Could not malloc page tables.\n");
            USLOSS_Halt(1);
        }
        initPageTable(i);
    }

    // Init the frame table
    NumFrames = frames;
    FrameTable = malloc(frames * sizeof(Frame));
    for (int i = 0; i < frames; i++)
    {
        FrameTable[i].page = EMPTY;
        FrameTable[i].pid = EMPTY;
        FrameTable[i].locked = FALSE;
    }
    FramesMutex = createMutex();

    // Create the fault mailbox.
    FaultsMbox = MboxCreate(MAXPROC, MAX_MESSAGE);
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("vmInitReal(): FaultsMbox created as %d\n", FaultsMbox);
    }

    /*
     * Fork the pagers.
     */
    NumPagers = pagers;
    for (int i = 0; i < pagers; i++)
    {
        PagerPIDs[i] = fork1("Pager", Pager, NULL, USLOSS_MIN_STACK, 2);
        if(PagerPIDs[i] < 0)
        {
          USLOSS_Console("vmInitReal(): Can't create Pager %d\n", i);
          USLOSS_Halt(1);
        }
    }
    for (int i = pagers; i < MAXPAGERS; i++)
    {
        PagerPIDs[i] = -1;
    }

    // Zero out, then initialize, the vmStats structure
    initVmStats(&vmStats, pages, frames);

    VMInitialized = TRUE;
    int dummy;
    return USLOSS_MmuRegion(&dummy);
} /* vmInitReal */

/*
 *----------------------------------------------------------------------
 *
 * PrintStats --
 *
 *      Print out VM statistics.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Stuff is printed to the USLOSS_Console.
 *
 *----------------------------------------------------------------------
 */
void PrintStats(void)
{
    lockMutex(vmStatsMutex);
    USLOSS_Console("VmStats\n");
    USLOSS_Console("pages:          %d\n", vmStats.pages);
    USLOSS_Console("frames:         %d\n", vmStats.frames);
    USLOSS_Console("diskBlocks:     %d\n", vmStats.diskBlocks);
    USLOSS_Console("freeFrames:     %d\n", vmStats.freeFrames);
    USLOSS_Console("freeDiskBlocks: %d\n", vmStats.freeDiskBlocks);
    USLOSS_Console("switches:       %d\n", vmStats.switches);
    USLOSS_Console("faults:         %d\n", vmStats.faults);
    USLOSS_Console("new:            %d\n", vmStats.new);
    USLOSS_Console("pageIns:        %d\n", vmStats.pageIns);
    USLOSS_Console("pageOuts:       %d\n", vmStats.pageOuts);
    USLOSS_Console("replaced:       %d\n", vmStats.replaced);
    unlockMutex(vmStatsMutex);

    if (DEBUG5 && debugflag5)
    {
        dumpProcesses();
    }
} /* PrintStats */

/*
 *----------------------------------------------------------------------
 *
 * vmDestroyReal --
 *
 * Called by vmDestroy.
 * Frees all of the global data structures
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The MMU is turned off.
 *
 *----------------------------------------------------------------------
 */
void vmDestroyReal()
{
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("vmDestroyReal(): called.\n");
    }

    CheckMode();
    int result = USLOSS_MmuDone();

    /*
     * Kill the pagers here.
     */
    PagerKillSem = semcreateReal(0);
    for (int i = 0; i < NumPagers; i++)
    {
        int kill = -1;
        MboxSend(FaultsMbox, &kill, sizeof(int));
        sempReal(PagerKillSem);
    }

    if (result != USLOSS_MMU_OK)
    {
        USLOSS_Console("vmDestroyReal(): Result of MMuDone is not OK: %d.\n", result);
    }

    // Print vm statistics.
    PrintStats();

    VMInitialized = FALSE;

    // Free any allocated memory
    for (int i = 0; i < MAXPROC; i++)
    {
        Process *proc = getProc(i);
        free(proc->pageTable);
    }
    free(FrameTable);

} /* vmDestroyReal */

/*
 *----------------------------------------------------------------------
 *
 * FaultHandler
 *
 * Handles an MMU interrupt. Simply stores information about the
 * fault in a queue, wakes a waiting pager, and blocks until
 * the fault has been handled.
 *
 * Results:
 * None.
 *
 * Side effects:
 * The current process is blocked until the fault is handled.
 *
 *----------------------------------------------------------------------
 */
static void FaultHandler(int type, void* offset)
{
    int pid = getpid();

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("FaultHandler(%d): called.\n", pid);
    }

    assert(type == USLOSS_MMU_INT);
    int cause = USLOSS_MmuGetCause();
    assert(cause == USLOSS_MMU_FAULT);
    
    // Update vmStats
    lockMutex(vmStatsMutex);
    vmStats.faults++;
    unlockMutex(vmStatsMutex);

    int failure = TRUE;
    while (failure)
    {
        // Fill in the fault message
        FaultMsg *faultMsg = faults + (getpid() % MAXPROC);
        faultMsg->addr = offset;
        faultMsg->pid = pid;
        faultMsg->failed = FALSE;
        faultMsg->shouldTerminate = FALSE;

        // Send to pager
        if (DEBUG5 && debugflag5)
        {
            USLOSS_Console("FaultHandler(%d): Sending fault for address %p.\n", pid, offset);
        }
        int result = MboxSend(FaultsMbox, &pid, sizeof(int));
        if (result != 0)
        {
            USLOSS_Console("FaultHandler(%d): MboxSend failed.\n", pid);
        }

        if (DEBUG5 && debugflag5)
        {
            USLOSS_Console("FaultHandler(%d): Waiting for Pager.\n", pid);
        }

        // Wait for reply
        semPProc();
        if (DEBUG5 && debugflag5)
        {
            USLOSS_Console("FaultHandler(%d): Returned from page fault.\n", pid);
        }

        if (faultMsg->shouldTerminate)
        {
            terminateReal(1);
        }

        failure = faultMsg->failed;
        if (!failure)
        {
            lockMutex(FramesMutex);
            FrameTable[faultMsg->receivedFrame].locked = FALSE;
            unlockMutex(FramesMutex);
        }
    }
} /* FaultHandler */

/*
 *----------------------------------------------------------------------
 *
 * Pager
 *
 * Kernel process that handles page faults and does page replacement.
 *
 * Results:
 * None.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */
static int Pager(char *arg)
{
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("Pager(): called.\n");
    }
    while (TRUE)
    {
        // Kill pager if we are zapped
        if (isZapped())
        {
            break;
        }

        // Wait for fault to occur (receive from mailbox)
        int pid;
        int result = MboxReceive(FaultsMbox, &pid, sizeof(int));
        if (result < 0)
        {
            USLOSS_Console("Pager(): MboxReceive failed with error code %d.\n", result);
            break;
        }

        // Kill the pager if we got the killcode
        if (pid < 0)
        {
            break;
        }

        // Get the fault info from the array
        FaultMsg *fault = &faults[pid];
        int incomingPage = (int) ((long) fault->addr / USLOSS_MmuPageSize());
        if (DEBUG5 && debugflag5)
        {
            USLOSS_Console("Pager(): Fault for address %p, page number %d.\n", fault->addr, incomingPage);
        }

        // Check if incoming page is new
        Process *proc = getProc(pid);
        int incomingPageExists = proc->pageTable[incomingPage].state != UNUSED;

        // Find the frame to replace
        lockMutex(FramesMutex);
        int frame = getNextFrame();
        unlockMutex(FramesMutex);
        if (frame == EMPTY)
        {
            fault->failed = TRUE;
            semVProc(pid);
            continue;
        }
        fault->receivedFrame = frame;
        int outgoingPage = FrameTable[frame].page;
        Process *outgoingPageProc = getProc(FrameTable[frame].pid);

        // Update vmStats
        if (!incomingPageExists)
        {
            lockMutex(vmStatsMutex);
            vmStats.new++;
            unlockMutex(vmStatsMutex);
        }

        // Update the tables
        if (outgoingPage != EMPTY)
        {
            outgoingPageProc->pageTable[outgoingPage].state = ONDISK;
            outgoingPageProc->pageTable[outgoingPage].frame = EMPTY;
        }
        FrameTable[frame].page = incomingPage;
        FrameTable[frame].pid = pid;
        FrameTable[frame].locked = TRUE;
        proc->pageTable[incomingPage].state = INMEM;
        proc->pageTable[incomingPage].frame = frame;

        // Check the access bits
        int access;
        result = USLOSS_MmuGetAccess(frame, &access);
        if (result != USLOSS_MMU_OK)
        {
            USLOSS_Console("Pager(): Could not read frame access bits.\n");
            USLOSS_Halt(1);
        }

        // Write to disk if necessary
        if (outgoingPage != EMPTY && (access & USLOSS_MMU_DIRTY))
        {
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("Pager(): Writing page %d to disk for pid %d.\n", outgoingPage, outgoingPageProc->pid);
            }

            // Get the appropriate disk block
            int block = outgoingPageProc->pageTable[outgoingPage].diskBlock;
            if (block == EMPTY)
            {
                if (NextBlock == vmStats.diskBlocks)
                {
                    USLOSS_Console("Pager(): Swap disk has run out of space.\n");
                    fault->shouldTerminate = TRUE;
                    semVProc(pid);
                    continue;
                }

                // Find a new block
                block = NextBlock++;
                outgoingPageProc->pageTable[outgoingPage].diskBlock = block;
            }

            // Read the buffer from the vmRegion and write to disk
            char buffer[USLOSS_MmuPageSize()];
            result = USLOSS_MmuMap(TAG, outgoingPage, frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("Pager(): Could not perform mapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }
            memcpy(buffer, page(outgoingPage), USLOSS_MmuPageSize());
            result = USLOSS_MmuUnmap(TAG, outgoingPage);
            if (result != USLOSS_MMU_OK)
            {
                USLOSS_Console("Pager(): Could not perform unmapping. Error code %d.\n", result);
                USLOSS_Halt(1);
            }
            writePageToDisk(buffer, outgoingPageProc->pid, outgoingPage);
        }

        // Initialize the buffer to match the incoming page
        char buffer[USLOSS_MmuPageSize()];
        if (incomingPageExists && proc->pageTable[incomingPage].diskBlock != EMPTY)
        {
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("Pager(): Reading page %d from disk for pid %d.\n", incomingPage, pid);
            }
            readPageFromDisk(buffer, pid, incomingPage);
        }
        else
        {
            for (int i = 0; i < USLOSS_MmuPageSize(); i++)
            {
                buffer[i] = 0;
            }
        }

        // Write the buffer
        result = USLOSS_MmuMap(TAG, incomingPage, frame, USLOSS_MMU_PROT_RW);
        if (result != USLOSS_MMU_OK)
        {
            USLOSS_Console("Pager(): Could not perform mapping. Error code %d.\n", result);
            USLOSS_Halt(1);
        }
        memcpy(page(incomingPage), buffer, USLOSS_MmuPageSize());
        result = USLOSS_MmuUnmap(TAG, incomingPage);
        if (result != USLOSS_MMU_OK)
        {
            USLOSS_Console("Pager(): Could not perform unmapping. Error code %d.\n", result);
            USLOSS_Halt(1);
        }

        // Mark the buffer as clean
        result = USLOSS_MmuSetAccess(frame, access & ~USLOSS_MMU_DIRTY);
        if (result != USLOSS_MMU_OK)
        {
            USLOSS_Console("Pager(): Could not set frame access bits.\n");
            USLOSS_Halt(1);
        }

        if (DEBUG5 && debugflag5)
        {
            USLOSS_Console("Pager(): Finished paging page %d to frame %d for proc %d.\n", incomingPage, frame, pid);
        }

        // Unblock the waiting process
        semVProc(pid);
    }
    semvReal(PagerKillSem);
    return 0;
} /* Pager */
