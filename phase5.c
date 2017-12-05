/*
 * phase5.c
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
FaultMsg faults[MAXPROC];

// Vm Stats
VmStats vmStats;
int vmStatsMutex;

// Pager info
int NumPagers;
int PagerPIDs[MAXPAGERS];
int FaultsMbox;
int destroySem;
int pagerBlockSem;

// Start of the Vm Region
void *vmRegion;

// Frame table
Frame *FrameTable;
int NextCheckedFrame = 0;

// The Disk Table
DiskBlock *DiskTable;
int NextBlock = 0;

// Global Mmu info
int VMInitialized = 0;
int NumPages = 0;
int NumFrames = 0;

static void FaultHandler(int, void *);
static void PrintStats();
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
    systemCallVec[SYS_MBOXCREATE]      = mboxCreate;
    systemCallVec[SYS_MBOXRELEASE]     = mboxRelease;
    systemCallVec[SYS_MBOXSEND]        = mboxSend;
    systemCallVec[SYS_MBOXRECEIVE]     = mboxReceive;
    systemCallVec[SYS_MBOXCONDSEND]    = mboxCondSend;
    systemCallVec[SYS_MBOXCONDRECEIVE] = mboxCondReceive;

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
    pagerBlockSem = MboxCreate(0, 0);
    if (pagerBlockSem < 0)
    {
        USLOSS_Console("vmInitReal(): Could not create pager blocking semaphore.\n");
        USLOSS_Halt(1);
    }

    // Zero out, then initialize, the vmStats structure
    initVmStats(&vmStats, pages, frames);

    VMInitialized = 1;
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
    destroySem = semcreateReal(0);
    for (int i = 0; i < NumPagers; i++)
    {
        int kill = -1;
        MboxSend(FaultsMbox, &kill, sizeof(int));
        sempReal(destroySem);
    }

    if (result != USLOSS_MMU_OK)
    {
        USLOSS_Console("vmDestroyReal(): Result of MMuDone is not OK: %d.\n", result);
    }

    // Print vm statistics.
    PrintStats();

    //TODO: Free any allocated memory


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
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("FaultHandler(): called.\n");
    }

    assert(type == USLOSS_MMU_INT);
    int cause = USLOSS_MmuGetCause();
    assert(cause == USLOSS_MMU_FAULT);
    vmStats.faults++;

    for (int i = 0; i < NumFrames; i++)
    {
        if (!FrameTable[i].locked)
        {
            MboxCondSend(pagerBlockSem, NULL, 0);
            break;
        }
    }

    /*
     * Fill in faults[pid % MAXPROC], send it to the pagers, and wait for the
     * reply.
     */
    FaultMsg* currentMsgPtr = &faults[getpid()%MAXPROC];
    currentMsgPtr->addr = offset;
    currentMsgPtr->pid = getpid();
    currentMsgPtr->replyMbox = FaultsMbox; // TODO figure this out

    // Send to pagers
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("FaultHandler(): Sending fault for address %p.\n", currentMsgPtr->addr);
    }
    int pid = getpid();
    int result = MboxSend(FaultsMbox, &pid, sizeof(int));
    if (result != 0)
    {
        USLOSS_Console("FaultHandler(): MboxSend failed.\n");
    }

    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("FaultHandler(): Waiting for Pager.\n");
    }

    // Wait for reply
    semPProc();
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("FaultHandler(): Returned from page fault.\n");
    }

    int canUnlock = 0;
    for (int i = 0; i < NumFrames; i++)
    {
        if (!FrameTable[i].locked)
        {
            canUnlock = 1;
            break;
        }
    }
    FrameTable[currentMsgPtr->receivedFrame].locked = FALSE;
    if (canUnlock)
    {
        MboxCondSend(pagerBlockSem, NULL, 0);
    }

    // Enable interrupts
    enableInterrupts();
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
    while (1)
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
        if (!incomingPageExists)
        {
            vmStats.new++;
        }

        // Find the frame to replace
        int frame = getNextFrame();
        while (frame == -1)
        {
            // Block on the pager waiting semaphore
            MboxReceive(pagerBlockSem, NULL, 0);
            frame = getNextFrame();
        }
        fault->receivedFrame = frame;
        int outgoingPage = FrameTable[frame].page;
        Process *outgoingPageProc = getProc(FrameTable[frame].pid);

        int access;
        result = USLOSS_MmuGetAccess(frame, &access);
        if (result != USLOSS_MMU_OK)
        {
            USLOSS_Console("Pager(): Could not read frame access bits.\n");
            USLOSS_Halt(1);
        }

        // write to disk if necessary
        if (outgoingPage != EMPTY && (access & USLOSS_MMU_DIRTY))
        {
            if (DEBUG5 && debugflag5)
            {
                USLOSS_Console("Pager(): Writing page %d to disk for pid %d.\n", outgoingPage, pid);
            }

            // Get the appropriate disk block
            int block = outgoingPageProc->pageTable[outgoingPage].diskBlock;
            if (block == -1)
            {
                if (NextBlock == 64)
                {
                    USLOSS_Console("Pager(): Swap disk has run out of space.\n");
                    USLOSS_Halt(1);
                    // TODO should terminate proc instead of halt
                }

                // Find a new block
                block = NextBlock++;
                outgoingPageProc->pageTable[outgoingPage].diskBlock = block;
                // TODO update disk table
            }

            // Read the buffer from the vmRegion and write to disk TODO what if timesliced?
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
            writePageToDisk(buffer, pid, outgoingPage);
        }

        // Update the page table about the removal
        if (outgoingPage != EMPTY)
        {
            outgoingPageProc->pageTable[outgoingPage].state = ONDISK;
            outgoingPageProc->pageTable[outgoingPage].frame = -1;
        }

        // Initialize the buffer to match the page
        char buffer[USLOSS_MmuPageSize()];
        if (incomingPageExists && proc->pageTable[incomingPage].diskBlock != EMPTY)
        {
            readPageFromDisk(buffer, pid, incomingPage);
        }
        else
        {
            for (int i = 0; i < USLOSS_MmuPageSize(); i++)
            {
                buffer[i] = 0;
            }
        }

        // Write the buffer TODO what if we are timesliced with the temp mapping?
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

        FrameTable[frame].page = incomingPage;
        FrameTable[frame].pid = pid;
        FrameTable[frame].locked = TRUE;

        // Mark the buffer as clean
        result = USLOSS_MmuSetAccess(frame, access & ~USLOSS_MMU_DIRTY);
        if (result != USLOSS_MMU_OK)
        {
            USLOSS_Console("Pager(): Could not set frame access bits.\n");
            USLOSS_Halt(1);
        }

        // Update the page table
        proc->pageTable[incomingPage].state = INMEM;
        proc->pageTable[incomingPage].frame = frame;

        if (DEBUG5 && debugflag5)
        {
            USLOSS_Console("Pager(): Finished paging page %d to frame %d for proc %d.\n", incomingPage, frame, pid);
        }

        // Unblock the waiting process
        semVProc(pid);
    }
    semvReal(destroySem);
    return 0;
} /* Pager */
