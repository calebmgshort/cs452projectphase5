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

static Process ProcTable[MAXPROC];
FaultMsg faults[MAXPROC];
VmStats  vmStats;
int vmStatsMutex;
void *vmRegion;
int FaultsMbox;
int PagerPIDs[MAXPAGERS];

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

    // Init the Mmu
    int status = USLOSS_MmuInit(mappings, pages, frames, USLOSS_MMU_MODE_TLB);
    if (status != USLOSS_MMU_OK)
    {
       USLOSS_Console("vmInitReal(): couldn't initialize MMU, status %d\n", status);
       USLOSS_Halt(1);
    }
    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

    // Initial testing
    status = USLOSS_MmuMap(TAG, 0, 0, USLOSS_MMU_PROT_RW);
    if(status == USLOSS_MMU_ERR_REMAP)
    {
        USLOSS_Console("vmInitReal(): Could not map page 0 to frame 0\n");
    }

    /*
     * Initialize page tables.
     */

    // Create the fault mailbox.
    FaultsMbox = MboxCreate(MAXPROC, MAX_MESSAGE);

    /*
     * Fork the pagers.
     */
    for (int i = 0; i < pagers; i++)
    {
        PagerPIDs[i] = fork1("Pager", Pager, NULL, USLOSS_MIN_STACK, 2);
        if(PagerPIDs[i] < 0)
        {
          USLOSS_Console("vmInitReal(): Can't create Pager %d\n", i);
          USLOSS_Halt(1);
        }
    }
    for (int i = pagers; i < MAXPAGERS; i++){
      PagerPIDs[i] = -1;
    }

    // Zero out, then initialize, the vmStats structure
    initVmStats(&vmStats, pages, frames);

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
void vmDestroyReal(void)
{
    if (DEBUG5 && debugflag5)
    {
        USLOSS_Console("vmDestroyReal(): called.\n");
    }

    CheckMode();
    int result = USLOSS_MmuDone();
    if(result != USLOSS_MMU_OK)
    {
        USLOSS_Console("vmDestroyReal(): Result of MMuDone is not OK: %d.\n", result);
    }

    //Kill the pagers here.
    for(int i = 0; i < MAXPAGERS; i++)
    {
        if(PagerPIDs[i] >= 0)
        {
            zap(PagerPIDs[i]);
        }
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
   /*
    * TODO: Fill in faults[pid % MAXPROC], send it to the pagers, and wait for the
    * reply.
    */
   FaultMsg currentMsg = faults[getpid()%MAXPROC];
   currentMsg.addr = offset;
   currentMsg.pid = getpid()%MAXPROC;
   currentMsg.replyMbox = NULL;   // TODO: change


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
        USLOSS_Console("Pager(): started.\n");
    }

    while (1)
    {
        if (DEBUG5 && debugflag5)
        {
            USLOSS_Console("Pager(): called.\n");
        }
        // TODO
        /* Wait for fault to occur (receive from mailbox) */
        /* Look for free frame */
        /* If there isn't one then use clock algorithm to
         * replace a page (perhaps write to disk) */
        /* Load page into frame from disk, if necessary */
        /* Unblock waiting (faulting) process */
    }
    return 0;
} /* Pager */
