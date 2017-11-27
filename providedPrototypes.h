/*
 * Function prototypes from Patrick's phase3 solution. These can be called
 * when in *kernel* mode to get access to phase3 functionality.
 */


#ifndef PROVIDED_PROTOTYPES_H

#define PROVIDED_PROTOTYPES_H

extern int  spawnReal(char *, int (*func)(char *), char *,
                       int, int);
extern int  waitReal(int *);
extern void terminateReal(int);
extern int  semcreateReal(int);
extern int  sempReal(int);
extern int  semvReal(int);
extern int  semfreeReal(int);
extern int  gettimeofdayReal(int *);
extern int  cputimeReal(int *);
extern int  getPID_real(int *);

extern void* vmInitReal(int ,int ,int ,int);

#endif  /* PROVIDED_PROTOTYPES_H */
