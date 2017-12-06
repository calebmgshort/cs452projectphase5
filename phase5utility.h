/*
 * phase5utility.h
 */

#ifndef _PHASE5UTILITY_H
#define _PHASE5UTILITY_H

#include "vm.h"

extern void setToUserMode();
void initPageTable(int pid);
extern int createMutex();
extern void lockMutex(int);
extern void unlockMutex(int);
extern void initVmStats(VmStats *, int, int);
extern Process *getProc(int);
extern void semPProc();
extern void semVProc(int);
extern void enableInterrupts();
extern void dumpMappings();
extern int getNextFrame();
extern void *page(int);
extern void writePageToDisk(char *, int, int);
extern void readPageFromDisk(char *, int, int);
#endif
