/*
 * vm.h
 */
#ifndef _VM_H
#define _VM_H

#define DEBUG5 1

#define EMPTY -1
#define FALSE 0
#define TRUE 1

/*
 * All processes use the same tag.
 */
#define TAG 0

/*
 * Different states for a page.
 */
#define UNUSED 500
#define INMEM  501
#define ONDISK 502

/*
 * Page table entry.
 */
typedef struct PTE
{
    int  state;      // See above.
    int  frame;      // Frame that stores the page (if any). -1 if none.
    int  diskBlock;  // Disk block that stores the page (if any). -1 if none.
} PTE;

/*
 * Per-process information.
 */
typedef struct Process
{
    int pid;                // The pid of the process stored in this entry
    PTE *pageTable;         // The page table for the process.
    int privateSem;         // The id of the private mailbox used to block this process
} Process;

/*
 * Information about page faults. This message is sent by the faulting
 * process to the pager to request that the fault be handled.
 */
typedef struct FaultMsg
{
    int  pid;        // Process with the problem.
    void *addr;      // Address that caused the fault.
    int  replyMbox;  // Mailbox to send reply.
    // Add more stuff here.
} FaultMsg;

/*
 * A frame in the global frame table.
 */
typedef struct Frame
{
    int page;       // The page loaded into this frame
} Frame;

typedef struct DiskBlock
{
    int page;       // The page stored in this block
} DiskBlock;

extern int vmStatsMutex;

#define CheckMode() assert(USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE)

#endif
