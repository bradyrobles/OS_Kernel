/* ------------------------------------------------------------------------
	phase1.c
	Brady Robles & John Crawford
	CSCV 452

   ------------------------------------------------------------------------ */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <phase1.h>
#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
void startup();
void finish();
int fork1(char *, int(*)(char *), char *, int, int);

void launch();
int join(int *);
void quit(int);
void dispatcher(void);
int sentinel (char *);
int zap (int);
void init_ProcTable(int);
void addToReadyList(proc_ptr);
void removeFromReadyList(proc_ptr);
void removeFromQuitList(proc_ptr);
void removeFromChildList(proc_ptr);
void addToChildQuitList(proc_ptr);
int  block_me(int);
int  unblock_proc(int);
int  getpid();
int  is_zapped();
int  read_cur_start_time();
void time_slice();
void clock_handler();
static void check_deadlock();
void enableInterrupts();
void disableInterrupts();
void dump_processes();

extern int start1 (char *);


/* -------------------------- Globals ------------------------------------- */

/* Debugging global variable */
int debugflag = 0;


/* Process table */
proc_struct ProcTable[MAXPROC];

/* Process lists  */
static proc_ptr ReadyList;

/* Current process ID */
proc_ptr Current;

/* Next pid to be assigned */
unsigned int next_pid = SENTINELPID;


/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name         - startup
   Purpose      - Initializes process lists and clock interrupt vector.
	              Start up sentinel process and the test process.
   Parameters   - none, called by USLOSS
   Returns      - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup()
{
   int i;      /* loop index */
   int result; /* value returned by call to fork1() */

   /* initialize the process table */
   if (DEBUG && debugflag){
	   console("startup(): initializing the Process Table\n");
   }

   for (i = 0; i < MAXPROC; i++)
	   init_ProcTable(i);

   /* Initialize the Ready list, etc. */
   if (DEBUG && debugflag){
      console("startup(): initializing the Ready & Blocked lists\n");
   }
   ReadyList = NULL;

   /* Initialize the clock interrupt handler */
   int_vec[CLOCK_INT] = clock_handler;

   /* startup a sentinel process */
   if (DEBUG && debugflag)
       console("startup(): calling fork1() for sentinel\n");

   result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                   SENTINELPRIORITY);

   if (result < 0) {
      if (DEBUG && debugflag)
         console("startup(): fork1 of sentinel returned error, halting...\n");
      halt(1);
   }
  
   /* start the test process */
   if (DEBUG && debugflag)
      console("startup(): calling fork1() for start1\n");

   result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
   if (result < 0) {
      console("startup(): fork1 for start1 returned an error, halting...\n");
      halt(1);
   }

   console("startup(): Should not see this message! ");
   console("Returned from fork1 call that created start1\n");

   return;
} /* startup */


/* ------------------------------------------------------------------------
   Name         - finish
   Purpose      - Required by USLOSS
   Parameters   - none
   Returns      - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish()
{
   if (DEBUG && debugflag)
      console("finishing...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name         - fork1
   Purpose      - Gets a new process from the process table and initializes
                  information of the process.  Updates information in the
                  parent process to reflect this child process creation.
   Parameters   - the process procedure address, the size of the stack and
                  the priority to be assigned to the child process.
   Returns      - the process id of the created child or -1 if no child could
                  be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*f)(char *), char *arg, int stacksize, int priority)
{
   int proc_slot = -1;

   if (DEBUG && debugflag)
         console("fork1(): creating process %s\n", name);

   /* test if in kernel mode; halt if in user mode */
   if ((PSR_CURRENT_MODE & psr_get()) == 0 ){
	   console("fork1(): called while in user mode, by process %d.  Halting...\n", Current->pid);
	   halt(1);
   }

   /* Disable Interrupts to prevent race conditions*/
   if (DEBUG && debugflag){
	   console("fork1(): Process %s is disabling interrupts.\n", name);
   }
   disableInterrupts();

   /* Return if stack size is too small */
   if (stacksize < USLOSS_MIN_STACK){
	   if (DEBUG && debugflag){
		   console("fork1(): Process %s stack size is too small.\n", name);
	   }
	   return -2;
   }

   /* Return if priority is not in bounds */
   if ((next_pid != SENTINELPID) && (priority > MINPRIORITY || priority < MAXPRIORITY)){
	   if (DEBUG && debugflag){
		   console("fork1(): Process %s priority is out of bounds\n", name);
	   }
	   return -1;
   }

   /* Return if process name is too long */
   if ( strlen(name) >= (MAXNAME - 1) ) {
      console("fork1(): Process name is too long.  Halting...\n");
      halt(1);
   }
   printf("here");

   /* find an empty slot in the process table */
   proc_slot = get_Proc_Slot();

   if (proc_slot == -1){
	   if (DEBUG && debugflag){
		   console ("fork1(): process table full\n");
	   }
	   return -1;
   }

   /* fill-in entry in process table */
   ProcTable[proc_slot].pid = next_pid;
   strcpy(ProcTable[proc_slot].name, name);
   ProcTable[proc_slot].start_func = f;

   /* Check if arg is correct */
   if ( arg == NULL )
      ProcTable[proc_slot].start_arg[0] = '\0';
   else if ( strlen(arg) >= (MAXARG - 1) ) {
      console("fork1(): argument too long.  Halting...\n");
      halt(1);
   }
   else
      strcpy(ProcTable[proc_slot].start_arg, arg);

   /* Check if malloc is successfully called */
   ProcTable[proc_slot].stacksize = stacksize;
   if ((ProcTable[proc_slot].stack = malloc(stacksize)) == NULL){
	   console("fork1(): malloc failed.  Halting...\n");
	   halt(1);
   }
   ProcTable[proc_slot].priority = priority;

   /* Setting Child, Sibling, and Parent pointers */
   if (Current != NULL){
	   Current->kids++;

	   // Current has no children
	   if (Current->child_proc_ptr == NULL){
		   Current->child_proc_ptr = &ProcTable[proc_slot];
	   }
	   // Current Has children
	   else{
		   proc_ptr child = Current->child_proc_ptr;

		   // Insert child at end of sibling list
		   while (child->next_sibling_ptr != NULL){
			   child = child->next_sibling_ptr;
		   }
		   child->next_sibling_ptr = &ProcTable[proc_slot];
	   }
   }
   ProcTable[proc_slot].parent_ptr = Current;

   /* Initialize context for this process, but use launch function pointer for
    * the initial value of the process's program counter (PC)
    */
   context_init(&(ProcTable[proc_slot].state), psr_get(),
                ProcTable[proc_slot].stack, 
                ProcTable[proc_slot].stacksize, launch);

   /* for future phase(s) */
   p1_fork(ProcTable[proc_slot].pid);

   ProcTable[proc_slot].status = READY;

   addToReadyList(&ProcTable[proc_slot]);
   next_pid++;

   // Dispatcher should not be called if Sentinel is proc
   if (ProcTable[proc_slot].pid != SENTINELPID){
	   dispatcher();
   }
   return ProcTable[proc_slot].pid;

} /* fork1 */

/* ------------------------------------------------------------------------
   Name         - launch
   Purpose      - Dummy function to enable interrupts and launch a given process
                  upon startup.
   Parameters   - none
   Returns      - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
   int result;

   if (DEBUG && debugflag)
      console("launch(): started\n");

   /* Enable interrupts */
   enableInterrupts();

   /* Call the function passed to fork1, and capture its return value */
   result = Current->start_func(Current->start_arg);

   if (DEBUG && debugflag)
      console("Process %d returned to launch\n", Current->pid);

   quit(result);

} /* launch */


/* ------------------------------------------------------------------------
   Name         - join
   Purpose      - Wait for a child process (if one has been forked) to quit.  If
                  one has already quit, don't wait.
   Parameters   - a pointer to an int where the termination code of the
                  quitting process is to be stored.
   Returns      - the process id of the quitting child joined on.
		          -1 if the process was zapped in the join
		          -2 if the process has no children
		          -3 if child quit before join occurred
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *code)
{
	int child_pid = -3;
	proc_ptr child;

	/* Make sure kernel mode is active */
	if ((PSR_CURRENT_MODE & psr_get()) == 0){
			//not in kernel mode
			console("join(): Not in kernel mode, may not call join()\n");
			halt(1);
	}

	disableInterrupts();

	/* Process has no children */
	if (Current->child_proc_ptr == NULL && Current->quit_child_ptr == NULL){
		if (DEBUG && debugflag){
			console("join(): Process %s has no children.\n", Current->name);
		}
		return -2;
	}

	/* No children have quit	 */
	if (Current->quit_child_ptr == NULL){
		Current->status = JOIN_BLOCK;
		removeFromReadyList(Current);

		if (DEBUG && debugflag){
			console("join(): %s is blocked.\n", Current->name);
		}
		dispatcher();
	}

	/* Children quit before the join occurred */
	child = Current->quit_child_ptr;

	child_pid = child->pid;
	*code = child->quit;
	removeFromQuitList(child);
	init_ProcTable(child_pid);

	if (is_zapped())
		return -1;

	return child_pid;
} /* join */


/* ------------------------------------------------------------------------
   Name         - quit
   Purpose      - Stops the child process and notifies the parent of the death by
                  putting child quit info on the parents child completion code
                  list.
   Parameters   - the code to return to the grieving parent
   Returns      - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int code)
{
	int curr_pid = -1;

	/* Make sure kernel is active */
	if ((PSR_CURRENT_MODE & psr_get()) == 0){
		//not in kernel mode
		console("quit(): Not in kernel mode, process %s may not call quit()\n", Current->name);
		halt(1);
	}

	if (DEBUG && debugflag){
		console("quit(): Process %s is disabling interrupts.\n", Current->name);
	}
	disableInterrupts();

	/* Error if process calls quit() with an active child */
	if (Current->child_proc_ptr != NULL){
		console("quit(): Process %s has active children. Halting...\n", Current->name);
		halt(1);
	}

	Current->quit = code;
	Current->status = QUIT;
	removeFromReadyList(Current);

	/* Unblock all process that zapped this process */
	if (is_zapped()){
		proc_ptr temp = Current->zapped;
		while (temp != NULL){
			temp->status = READY;
			addToReadyList(temp);
			temp = temp->next_zapped;
		}
	}

	/* Parent has quit children  */
	if (Current->parent_ptr != NULL && Current->quit_child_ptr != NULL){

		/* Clean up children on quit list */
		while (Current->quit_child_ptr != NULL){
			int child_pid = Current->quit_child_ptr->pid;
			removeFromQuitList(Current->quit_child_ptr);
			init_ProcTable(child_pid);
		}

		/* Clean up Current */
		Current->parent_ptr->status = READY;
		removeFromChildList(Current);
		addToChildQuitList(Current->parent_ptr);
		addToReadyList(Current->parent_ptr);
		curr_pid = Current->pid;

	} else if (Current->parent_ptr != NULL){

		/* Process is a child */
		addToChildQuitList(Current->parent_ptr);
		removeFromChildList(Current);

		if (Current->parent_ptr->status == JOIN_BLOCK){
			addToReadyList(Current->parent_ptr);
			Current->parent_ptr->status = READY;
		}

	}else{
		/* Process is a parent */
		while (Current->quit_child_ptr != NULL){
			int child_pid = Current->quit_child_ptr->pid;
			removeFromQuitList(Current->quit_child_ptr);
			init_ProcTable(child_pid);
		}
		curr_pid = Current->pid;
		init_ProcTable(Current->pid);

	}
	if (Current->parent_ptr != NULL){
		Current->parent_ptr->kids--;
	}
   p1_quit(curr_pid);
   dispatcher();
} /* quit */


/* ------------------------------------------------------------------------
   Name         - dispatcher
   Purpose      - dispatches ready processes.  The process with the highest
                  priority (the first on the ready list) is scheduled to
                  run.  The old process is swapped out and the new process
                  swapped in.
   Parameters   - none
   Returns      - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void)
{
   if (DEBUG && debugflag){
	   console("dispatcher(): started.\n");
   }

   /* Dispatch is called for the first time by start1() */
   if (Current == NULL){
	   Current = ReadyList;
	   Current->start_time = sys_clock();

	   /*Enable interrupts and change context */
	   enableInterrupts();
	   context_switch(NULL, &Current->state);

   }else{
	   proc_ptr temp = Current;

	   /*Previous process overran timesplice */
	   if (temp->status == RUNNING){
		   temp->status = READY;
	   }
	   Current->cputime += (sys_clock() - read_cur_start_time());

	   Current = ReadyList;
	   removeFromReadyList(Current);
	   Current->status = RUNNING;

	   // Put current in correct priority
	   addToReadyList(Current);

	   Current->start_time = sys_clock();
	   p1_switch(temp->pid, Current->pid);

	   /*Enable interrupts and change context */
	   enableInterrupts();
	   context_switch(&temp->state, &Current->state);
   }

} /* dispatcher */


/* ------------------------------------------------------------------------
   Name         - zap
   Purpose      - Marks a process as zapped.
   Parameters   - int pid - the process id to be zapped
   Returns      - -0 The zapped process called quit();
   	   	   	   	  -1 The calling process was zapped
   Side Effects - Process calling zap is added to the process's zapped
   	   	   	   	  list
   ----------------------------------------------------------------------- */
int zap(int pid){
	proc_ptr zap;

	/* Make sure kernel is active */
	if ((PSR_CURRENT_MODE & psr_get()) == 0){
		//not in kernel mode
		console("zap(): Not in kernel mode, process %s may not call zap()\n", Current->name);
		halt(1);
	}

	if (DEBUG && debugflag){
		console("zap(): Process %s is disabling interrupts.\n", Current->name);
	}
	disableInterrupts();

	/* Process tried to zap itself*/
	if (Current->pid == pid){
		console("zap(): Process %s tried to zap itself. Halting...\n", Current->name);
		halt(1);
	}

	/* Process to be zapped does not exist */
	if (ProcTable[pid % MAXPROC].status == EMPTY || ProcTable[pid % MAXPROC].pid != pid){
		console("zap(): Process being zapped does not exist.  Halting...\n");
		halt(1);
	}

	/* Process to zap has finished, waiting on parent to finish */
	if (ProcTable[pid % MAXPROC].status == QUIT){
		if (is_zapped()){
			return -1;
		}
		return 0;
	}

	Current->status = ZAP_BLOCK;
	removeFromReadyList(Current);
	zap = &ProcTable[pid % MAXPROC];
	zap->zap = 1;

	/* Add process to list of process zapped by this process */
	if (zap->zapped == NULL){
		zap->zapped = Current;
	}else{
		proc_ptr temp = zap->zapped;
		zap->zapped = Current;
		zap->zapped->next_zapped = temp;
	}

	dispatcher();
	if (is_zapped()){
		return -1;
	}
	return 0;


}

/* ------------------------------------------------------------------------
   Name         - sentinel
   Purpose      - The purpose of the sentinel routine is two-fold.  One
                  responsibility is to keep the system going when all other
	              processes are blocked.  The other is to detect and report
	              simple deadlock states.
   Parameters   - none
   Returns      - nothing
   Side Effects - if system is in deadlock, print appropriate error
		          and halt.
   ----------------------------------------------------------------------- */
int sentinel (char *dummy)
{
   if (DEBUG && debugflag)
      console("sentinel(): called\n");
   while (1)
   {
      check_deadlock();
      waitint();
   }
} /* sentinel */


/* ------------------------- Helper Functions ----------------------------- */

/* ------------------------------------------------------------------------
   Name         - dump_processes
   Purpose      - Loops through all process slots and prints out their contents
   Parameters   - void
   Returns      - void
   Side Effects - Process list is printed
   ----------------------------------------------------------------------- */
void dump_processes(){
	console("PID	Parent		Priority	Status		# Kids  CPUTime Name\n");

	for (int i = 0; i < MAXPROC; i++){
		console(" %d	", ProcTable[i].pid);
		if (ProcTable[i].parent_ptr != NULL){
			console("  %d    ", ProcTable[i].parent_ptr->pid);
		}else{
			console("   -1   ");
		}
		console("   %d		", ProcTable[i].priority);

		if (Current->pid == ProcTable[i].pid){
			console("RUNNING		");
		}else if (ProcTable[i].status == 5){
			console("EMPTY		  ");
		}else if (ProcTable[i].status == 4){
			console("BLOCKED	  ");
		}else if (ProcTable[i].status == 3){
			console("QUIT		  ");
		}else if (ProcTable[i].status == 1){
			console("READY		  ");
		}else if (ProcTable[i].status == 6){
			console("JOIN_BLOCK	  ");
		}else if (ProcTable[i].status == 7){
			console("ZAP_BLOCK	  ");
		}else{
			console("%d			  ", ProcTable[i].status);
		}

		console("  %d  ", ProcTable[i].kids);
		console("   %d", ProcTable[i].cputime);

		if (strcmp(ProcTable[i].name, "") != 0){
			console("	%s", ProcTable[i].name);
		}
		console("\n");
	}
}
/* ------------------------------------------------------------------------
   Name         - init_ProcTable
   Purpose      - Initialize a process struct, zero all attributes
   Parameters   - int pid - the process to be zeroed
   Returns      - void
   Side Effects - Attributes of the ProcTable for pid are zeroed
   ----------------------------------------------------------------------- */
void init_ProcTable(int pid){
	int i = pid % MAXPROC;
	ProcTable[i].next_proc_ptr = NULL;
	ProcTable[i].parent_ptr = NULL;

	ProcTable[i].child_proc_ptr = NULL;
	ProcTable[i].quit_child_ptr = NULL;

	ProcTable[i].next_sibling_ptr = NULL;
	ProcTable[i].next_quit_sibling_ptr = NULL;

	ProcTable[i].next_zapped = NULL;
	ProcTable[i].zapped = NULL;

	ProcTable[i].name[0] = '\0';
	ProcTable[i].start_arg[0] = '\0';

	ProcTable[i].pid = -1;
	ProcTable[i].priority = -1;
	ProcTable[i].start_func = NULL;
	ProcTable[i].stack = NULL;
	ProcTable[i].stacksize = -1;
	ProcTable[i].status = EMPTY;
	ProcTable[i].kids = 0;
	ProcTable[i].cputime = -1;
	ProcTable[i].quit = -1;
}/* init_ProcTable */

/* ------------------------------------------------------------------------
   Name         - addToReadyList
   Purpose      - Find spot in ReadList to store proc with regards to priority
   Parameters   - proc_ptr proc: the pointer to the process to be added
   Returns      - void
   Side Effects - Proc is added to Ready List
   ------------------------------------------------------------------------ */

void addToReadyList(proc_ptr proc){
	if (DEBUG && debugflag){
		console("addToReadyList(): Adding process %s to Ready list\n", proc->name);
	}

	if (ReadyList == NULL){
		ReadyList = proc;
	}
	else{
		// Proc has greatest priority in list
		if (ReadyList->priority > proc->priority){
			proc_ptr temp = ReadyList;
			ReadyList = proc;
			proc->next_proc_ptr = temp;
		}
		// Add proc to list after first greater priority
		else{
			proc_ptr next = ReadyList->next_proc_ptr;
			proc_ptr temp = ReadyList;

			while (next->priority <= proc->priority){
				temp = next;
				next = next->next_proc_ptr;
			}
			temp->next_proc_ptr = proc;
			proc->next_proc_ptr = next;
		}

	}
} /* addToReadyList */


/* ------------------------------------------------------------------------
   Name         - removeFromReadyList
   Purpose      - Find proc in Ready List, remove process from list, clean
   	   	   	   	  up Ready List by reassigning pointers
   Parameters   - proc_ptr proc: the pointer to the process to be removed
   Returns      - void
   Side Effects - Proc is removed from Ready List
   ------------------------------------------------------------------------ */
void removeFromReadyList(proc_ptr proc){
	if (DEBUG && debugflag){
			console("removeFromReadyList(): Removing process %s from Ready list\n", proc->name);
		}
	/* Process is head of ready list */
	if (proc == ReadyList){
		ReadyList = ReadyList->next_proc_ptr;
	}else{
		/* iterate through ReadyList until proc is found */
		proc_ptr temp = ReadyList;
		while (temp->next_proc_ptr != proc){
			temp = temp->next_proc_ptr;
		}
		temp->next_proc_ptr = temp->next_proc_ptr->next_proc_ptr;
	}

	if (DEBUG && debugflag){
		console("removeFromReadyList(): Removed process %s from Ready List", proc->name);
	}

} /* removeFromReadyList */


/*
 * Remove a process from its parents quit_list
 */
void removeFromQuitList(proc_ptr proc){
	proc->parent_ptr->quit_child_ptr = proc->next_quit_sibling_ptr;
}

/* ------------------------------------------------------------------------
   Name         - addToChildQuitList
   Purpose      - Store proc in proc quit children list
   Parameters   - proc_ptr proc: the pointer to the process to be added
   Returns      - void
   Side Effects - Proc is added to Quit Child List
   ------------------------------------------------------------------------ */
void addToChildQuitList(proc_ptr proc){
	if (proc->quit_child_ptr == NULL){
		proc->quit_child_ptr = Current;
		return;
	}

	proc_ptr temp = proc->quit_child_ptr;
	while(temp->next_quit_sibling_ptr != NULL){
		temp = temp->next_quit_sibling_ptr;
	}
	temp->next_quit_sibling_ptr = Current;
}

/* ------------------------------------------------------------------------
   Name         - removeFromChildList
   Purpose      - Find proc in Child List, remove process from list, clean
   	   	   	   	  up Child List by reassigning pointers
   Parameters   - proc_ptr proc: the pointer to the process to be removed
   Returns      - void
   Side Effects - Proc is removed from Child List
   ------------------------------------------------------------------------ */
void removeFromChildList(proc_ptr proc){

	/* Proc is at head of child_list */
	if (proc == proc->parent_ptr->child_proc_ptr){
		proc->parent_ptr->child_proc_ptr = proc->next_sibling_ptr;

	}else{
		proc_ptr temp = proc->parent_ptr->child_proc_ptr;

		while(temp->next_sibling_ptr != proc){
			temp = temp->next_sibling_ptr;
		}

		temp->next_sibling_ptr = temp->next_sibling_ptr->next_sibling_ptr;

	}

} /* removeFromChildList */


/* ------------------------------------------------------------------------
   Name         - block_me
   Purpose      - block a process and remove it from Ready List
   Parameters   - int status - the status of the process to block
   Returns      - -1 if process is zapped
   	   	   	   	  -0 if process successfully blocked
   Side Effects - Process' status is changed
   ------------------------------------------------------------------------ */
int block_me(int status){
	/* Check if we are in kernel mode */
	  if((PSR_CURRENT_MODE & psr_get()) == 0) {
	    //not in kernel mode
	    console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
	    halt(1);
	  }

	  if (DEBUG && debugflag){
		  console ("block_me(): Process %s is disabling interrupts.\n", Current->name);
	  }
	  disableInterrupts();

	  /* Check that status is valid */
	  if (status > 7){
		  console("block_me(): Process %s called with an invalid status of %d", Current->name, status);
		  halt(1);
	  }

	  /*Block current process */

	  Current->status = status;
	  removeFromReadyList(Current);
	  dispatcher();

	  if (is_zapped()){
		  return -1;
	  }
	  return 0;
}/*block_me*/


/* ------------------------------------------------------------------------
   Name         - unblock_proc
   Purpose      - unblocks a process
   Parameters   - int pid - the pid of the process to unblock
   Returns      - -1 if process is zapped
   	   	   	   	  -0 if process successfully unblocked
   Side Effects - Process' status is changed
   ------------------------------------------------------------------------ */
int unblock_proc(int pid){
	if (ProcTable[pid % MAXPROC].pid != pid){
		return -2;
	}

	if (Current->pid == pid){
		return -2;
	}

	if (ProcTable[pid % MAXPROC].status < 7){
		return -2;
	}

	if (is_zapped()){
		return -1;
	}

	ProcTable[pid % MAXPROC].status = READY;
	addToReadyList(&ProcTable[pid % MAXPROC]);
	dispatcher();
	return 0;
}

int get_Proc_Slot(){
	int index = next_pid % MAXPROC;
	int counter = 0;

	while (ProcTable[index].status != EMPTY){
		next_pid++;
		index = next_pid % MAXPROC;

		if(counter >= MAXPROC){
			return -1;
		}
		counter++;
	}
	return index;
}
/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
  /* turn the interrupts OFF if we are in kernel mode */
  if((PSR_CURRENT_MODE & psr_get()) == 0) {
    //not in kernel mode
    console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
    halt(1);
  } else{
		/* We ARE in kernel mode */
		if (DEBUG && debugflag) {
		        console("join(): Process %s is disabling interrupts.\n",
		                       Current->name);
		}
		/* disable interrupts */
		psr_set( psr_get() & ~PSR_CURRENT_INT );
	}
} /* disableInterrupts */


/*
 * Enables the interrupts.
 */
void enableInterrupts(){
	/* Turn the interrupts ON if we are in kernel mode */
	if ((PSR_CURRENT_MODE & psr_get()) == 0){
		//not in kernel mode
		console("Kernel Error: Not in kernel mode, may not enable interrupts\n");
		halt(1);
	} else{
		/* We ARE in kernel mode */
		if (DEBUG && debugflag) {
		        console("join(): Process %s is disabling interrupts.\n",
		                       Current->name);
		}
		/* enable interrupts */
		psr_set( psr_get() | PSR_CURRENT_INT);
	}
} /*enableInterrupts*/


/*
 * Check if deadlock has occurred.
 */
static void check_deadlock(){


	// Number of processes
	int numProc = 0;
	printf("hereherehere");

	// Check each process in ProcTable for status, add 1 if not empty
	for (int i = 0; i < MAXPROC; i++){
		if (ProcTable[i].status != EMPTY)
			numProc++;
	}
	// A deadlock occurred
	if (numProc > 1){
		console("check_deadlock(): numProc = %d", numProc);
		console("check_deadlock(): processes still present.   Halting...\n");
		halt(1);
	}

	console("All processes completed.\n");
	halt(0);


} /* check_deadlock */

/*
 * Check time
 */
void clock_handler(){
	time_slice();
}

/*
 * Check time against allowed time_slice
 */
void time_slice(){
	if ((sys_clock() - read_cur_start_time()) >= TIME_SLICE){
		dispatcher();
	}
	return;
}

int read_cur_start_time(){
	int curr_time = Current->start_time;
	return curr_time;
}

/*
 * is_zapped returns whether a process has been zapped
 */
int is_zapped(){
	return Current->zap;
}

/*
 * Return the pid of the current process
 */
int getpid(){
	return Current->pid;
}
