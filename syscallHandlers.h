/*
 * syscallHandlers.h
 */

#ifndef _SYSCALLHANDLERS_H
#define _SYSCALLHANDLERS_H

extern void vmInit(USLOSS_Sysargs *);
extern void vmDestroy(USLOSS_Sysargs *);

extern void mboxCreate(USLOSS_Sysargs *);
extern void mboxRelease(USLOSS_Sysargs *);
extern void mboxSend(USLOSS_Sysargs *);
extern void mboxReceive(USLOSS_Sysargs *);
extern void mboxCondSend(USLOSS_Sysargs *);
extern void mboxCondReceive(USLOSS_Sysargs *);

#endif
