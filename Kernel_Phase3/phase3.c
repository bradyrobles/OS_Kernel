/*
 * phase3.c
 *
 *  Brady Robles
 *  April 11, 2020
 *  CSCV 452 The Kernel Phase 3
 *
 */
#include <stdlib.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usyscall.h>
#include <sems.h>
#include <libuser.h>
#include <string.h>

/* ------------------------- Prototypes ----------------------------------- */
void spawn(sysargs *args);
int  spawn_real(char *name, int (*func)(char *), char *arg, int stackSize, int priority);
int  spawn_launch(char *arg);

void check_kernel_mode(char * processName);
void set_user_mode();

void wait1(sysargs *args);
int  wait_real(int *status);
void terminate(sysargs *args);

void nullsys3(sysargs *args);

void addChildToList(ProcessTablePtr child);
void removeChildFromList(ProcessTablePtr process);

void getPID(sysargs *args);
void getTimeOfDay(sysargs *args);
void cpuTime (sysargs *args);

int  start2(char *arg);
extern int start3(char *arg);


/* ------------------------- Globals ----------------------------------- */

ProcessTable procTable[MAXPROC]; // global process table

/* ------------------------- Functions --------------------------------- */

/* ---------------------------------------------------------------------
	Name         - start2
	Purpose      - initializes the process table, semaphore table, and system
				   call vector
	Parameters   - arg: function arguments
	Returns      - int: 0 for a normal quit
	Side Effects - initializes all phase3 structures
   --------------------------------------------------------------------- */
int start2(char *arg){
	int pid;    // start3 process ID
	int status; // start3 quit status

	// Check Kernel mode
	check_kernel_mode("start2");

	// initialize all structs in process table
	for (int i = 0; i < MAXPROC; i++){
		procTable[i].status = EMPTY;
	}

	// initialize all system call handlers to nullsys3
	for (int i = 0; i < MAXSYSCALLS; i++){
		sys_vec[i] = nullsys3;
	}

	//intialize the system call vector
	sys_vec[SYS_SPAWN]     = spawn;
	sys_vec[SYS_WAIT]      = wait1;
	sys_vec[SYS_TERMINATE] = terminate;
	sys_vec[SYS_GETPID]    = getPID;
	sys_vec[SYS_GETTIMEOFDAY] = getTimeOfDay;
	sys_vec[SYS_CPUTIME]   = cpuTime;

	pid = spawn_real("start3", start3, NULL, 4*USLOSS_MIN_STACK, 3);

	// start3 failed to create
	if (pid < 0){
		quit(pid);
	}

	pid = wait_real(&status);

	// start3 failed to join with child process
	if (pid < 0){
		quit(pid);
	}

	quit(0);
	return 0;

} /* start2 */

/* ---------------------------------------------------------------------
	Name         - spawn
	Purpose      - create a user-level process
	Parameters   - sysargs *args
					arg1: address of the function to spawn
					arg2: parameter passed to spawned function
					arg3: stack size
					arg4: priority
					arg5: character string of proc name
	Returns      - sets arg values
				    arg1: pid of the created process, -1 if process could
				    not be spawned
				    arg2: -1 if illegal values are given, 0 otherwise
	Side Effects - spawn_real is called using the changed parameters
   --------------------------------------------------------------------- */
void spawn(sysargs *args){
	long pid;

	//check for invalid args
	if ((long) args->number != SYS_SPAWN){
		args->arg4 = (void *) -1;
		return;
	}

	if ((long) args->arg3 < USLOSS_MIN_STACK){
		args->arg4 = (void *) -1;
		return;
	}
	if ((long) args->arg4 > MINPRIORITY || (long) args->arg4 < MAXPRIORITY){
		args->arg4 = (void *) -1;
		return;
	}

	pid = spawn_real((char *) args->arg5, args->arg1, args->arg2,
			         (long) args->arg3,(long) args->arg4);

	args->arg1 = (void *) pid; // id of the newly created process, -1 if failed
	args->arg4 = (void *) 0;
	set_user_mode();
} /* spawn */

/* ---------------------------------------------------------------------
	Name         - spawn_real
	Purpose      - Create a user-level process using fork1 from phase1
	Parameters   - func: address of the function to spawn
				   arg:  parameter passed to the spawned function
				   stackSize: stack size in bytes
				   priority: priority of the process to be spawned
				   name: character string of the process name
	Returns      - int: pid of the newly created process, -1 if failed
	Side Effects - proc is added to proc table
   --------------------------------------------------------------------- */
int spawn_real(char *name, int (*func)(char*), char *arg, int stackSize, int priority){
	int childPID; // pid to return
	int mboxID; // mailbox for the newly created process

	childPID = fork1(name, spawn_launch, arg, stackSize, priority);

	// check for error during fork1
	if (childPID < 0){
		return childPID;
	}

	//If parent process priority is higher than child, initialize
	//process table for child, else capture mboxID of child
	if (procTable[childPID % MAXPROC].status == EMPTY){
		mboxID = MboxCreate(0,0);
		procTable[childPID % MAXPROC].mboxID = mboxID;
		procTable[childPID % MAXPROC].status = ACTIVE;
	}else{
		mboxID = procTable[childPID % MAXPROC].mboxID;
	}

	// Complete initialization of child in procTable
	strcpy(procTable[childPID % MAXPROC].name, name);
	procTable[childPID % MAXPROC].pid = childPID;
	procTable[childPID % MAXPROC].priority = priority;
	procTable[childPID % MAXPROC].func = func;
	procTable[childPID % MAXPROC].childProcessPtr = NULL;
	procTable[childPID % MAXPROC].nextSiblingPtr = NULL;
	//procTable[childPID % MAXPROC].nextSemaphoreBlocked = NULL; // SEMAPHORE
	procTable[childPID % MAXPROC].stackSize = stackSize;

	if (arg == NULL){
		procTable[childPID % MAXPROC].startArg[0] = 0;
	}else{
		strcpy(procTable[childPID % MAXPROC].startArg, arg);
	}

	// disallow start2 from initializing in procTable
	if (getpid() != START2_PID){
		procTable[childPID % MAXPROC].parentPtr = &procTable[getpid() % MAXPROC];
		addChildToList(&procTable[childPID % MAXPROC]);
	}

	// wake up child if blocked
	MboxCondSend(mboxID, NULL, 0);

	return childPID;
} /* spawn_real */

/* ---------------------------------------------------------------------
	Name         - spawn_launch
	Purpose      - called by phase1 fork to execute the user-level process
	Parameters   - arg:  parameter passed to the spawned function
	Returns      - int: 0 everytime
	Side Effects - process will terminate
   --------------------------------------------------------------------- */
int spawn_launch(char *arg){
	int pid = getpid(); // process ID
	int userProcReturn; // value returned from user process
	int mboxID; // process mailbox

	// child is higher priority than parent, so procTable is not initialized
	if (procTable[pid % MAXPROC].status == EMPTY){
		procTable[pid % MAXPROC].status = ACTIVE;
		mboxID = MboxCreate(0, 0);
		procTable[pid % MAXPROC].mboxID = mboxID;
		MboxReceive(mboxID, NULL, 0);
	}

	//Terminate process if zapped
	if (is_zapped()){
		set_user_mode();
		Terminate(100); // CHANGE
	}

	set_user_mode();

	// calls func for child process
	userProcReturn = procTable[pid % MAXPROC].func(procTable[pid % MAXPROC].startArg);

	Terminate(userProcReturn);
	return 0;
} /* spawn_launch */

/* ---------------------------------------------------------------------
	Name         - wait
	Purpose      - wait for child process to terminate
	Parameters   - sysargs args
	Returns      - set arg values
				   arg1: process ID of the terminating child
				   arg2: termination code of child
	Side Effects - process is blocked if no terminating children
   --------------------------------------------------------------------- */
void wait1(sysargs *args){
	int status;
	long childPID;

	// check if syscall is correct, return if not
	if ((long) args->number != SYS_WAIT){
		args->arg2 = (void *) -1;
		return;
	}

	// retrive termination code of child
	childPID = wait_real(&status);

	procTable[getpid() % MAXPROC].status = ACTIVE;

	// process had no children
	if (childPID == -2){
		args->arg1 = (void *) 0;
		args->arg2 = (void *) -2;
	} else {
		args->arg1 = (void *) childPID;
		args->arg2 = ((void *) (long) status);
	}

	set_user_mode();
} /* wait */

/* ---------------------------------------------------------------------
	Name         - wait_real
	Purpose      - Set process table status of calling process and join
	Parameters   - status: termination code of child proc
	Returns      - int: proc ID of terminating child
	Side Effects - Proces is blocked if not terminating children
   --------------------------------------------------------------------- */
int wait_real(int *status){
	procTable[getpid() % MAXPROC].status = WAIT_BLOCK;
	return join(status);
} /* wait_real */

/* ---------------------------------------------------------------------
	Name         - terminate
	Purpose      - terminate the called process and all of its children
	Parameters   - sysargs args
	Returns      - NONE
	Side Effects - proc status is set to EMPTY
   --------------------------------------------------------------------- */
void terminate(sysargs *args){
	ProcessTablePtr parent = &procTable[getpid() % MAXPROC]; // initialize calling proc

	// zap child processes of proc
	if (parent->childProcessPtr != NULL){
		while (parent->childProcessPtr != NULL){
			zap(parent->childProcessPtr->pid);
		}
	}

	// remove terminating process from parents proc list
	if (parent->pid != START3_PID && parent->parentPtr != NULL){
		removeChildFromList(&procTable[getpid() % MAXPROC]);
	}

	// empty process attributes
	parent->status = EMPTY;
	quit((int) (long) args->arg1);
}

/* ------------------------- Helper Functions -------------------------- */

/* Halt USLOSS if not kernel mode*/
void check_kernel_mode(char * procName){
	if ((PSR_CURRENT_MODE & psr_get()) == 0){
		console("check_kernel_mode(): called while in user mode by process %s. Halting...\n", procName);
		halt(1);

	}
}

void nullsys3(sysargs *args){
	console("nullsys(): Invalid syscall %d. Terminating...\n", args->number);
	Terminate(1);
}

/* return getpid() value*/
void getPID(sysargs *args){
	args->arg1 = ((void *) (long) getpid());
	set_user_mode();
}

/* return USLOSS clock time */
void getTimeOfDay(sysargs *args){
	args->arg1 = ((void *) (long) sys_clock());
	set_user_mode();
}

/* return the cpu readtime value */
void cpuTime(sysargs *args){
	args->arg1 = ((void *) (long) readtime());
	set_user_mode();
}

/* set mode from kernel mode to user mode */
void set_user_mode(){
	psr_set(psr_get() & 14);
}

/* add child process to parent's child list */
void addChildToList(ProcessTablePtr proc){
	ProcessTablePtr parent = &procTable[getpid() % MAXPROC];

	// Parent has no children so add proc to the head of lsit
	if (parent->childProcessPtr == NULL){
		parent->childProcessPtr = proc;
	} else {
		// add child to end of linke list
		ProcessTablePtr temp = parent->childProcessPtr;
		while (temp->nextSiblingPtr != NULL){
			temp = temp->nextSiblingPtr;
		}
		temp->nextSiblingPtr = proc;
	}

}

/* remove child process to parents child list */
void removeChildFromList(ProcessTablePtr proc){
	ProcessTablePtr temp = proc;

	// process is at the head of the list
	if (proc == proc->parentPtr->childProcessPtr){
		proc->parentPtr->childProcessPtr = proc->nextSiblingPtr;
	} else{
		temp = proc->parentPtr->childProcessPtr;
		while (temp->nextSiblingPtr != proc){
			temp = temp->nextSiblingPtr;
		}
		temp->nextSiblingPtr = temp->nextSiblingPtr->nextSiblingPtr;
	}
}
