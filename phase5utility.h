#ifndef _PHASE5UTILITY_H
#define _PHASE5UTILITY_H

extern void setToUserMode();
extern void initProc(int, int);
extern int createMutex();
extern void lockMutex(int);
extern void unlockMutex(int);
extern void initVmStats(VmStats *, int, int);

#endif
