/*
 * syscallHandlers.h
 */

#ifndef _SYSCALLHANDLERS_H
#define _SYSCALLHANDLERS_H

extern void vmInit(USLOSS_Sysargs *);
extern void vmDestroy(USLOSS_Sysargs *);

extern void mbox_create(USLOSS_Sysargs *args_ptr);
extern void mbox_release(USLOSS_Sysargs *args_ptr);
extern void mbox_send(USLOSS_Sysargs *args_ptr);
extern void mbox_receive(USLOSS_Sysargs *args_ptr);
extern void mbox_condsend(USLOSS_Sysargs *args_ptr);
extern void mbox_condreceive(USLOSS_Sysargs *args_ptr);

#endif
