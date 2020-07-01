/*
 *  sems.h
 *
 *  Brady Robles
 *  April 10, 2020
 *  CSCV 452 The Kernel Phase 3
 */

#ifndef PHASE_3_SEMS_H_
#define PHASE_3_SEMS_H_


// define priorities
#define MINPRIORITY 5
#define MAXPRIORITY 1

// define start process pids
#define START2_PID  3
#define START3_PID  4

// define proc table status
#define EMPTY       0
#define ACTIVE      1
#define WAIT_BLOCK  2


#endif /* PHASE_3_SEMS_H_ */

typedef struct ProcessTable *ProcessTablePtr;


typedef struct ProcessTable{
	ProcessTablePtr  childProcessPtr;
	ProcessTablePtr  nextSiblingPtr;
	ProcessTablePtr  parentPtr;
	ProcessTablePtr  nextSemaphoreBlocked;
	char             name[MAXNAME];
	char             startArg[MAXARG];
	short            pid;
	int              priority;
	int (* func)     (char *);
	unsigned int     stackSize;
	int              status;
	int              mboxID;
}ProcessTable;
