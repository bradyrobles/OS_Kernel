/* ------------------------------------------------------------------------
	p1.c
	Brady Robles & John Crawford
	CSCV 452

   ------------------------------------------------------------------------ */

#include "usloss.h"
#define DEBUG 0
extern int debugflag;

/* ------------------------------------------------------------------------
   Name - p1_fork
   Purpose - Placeholder for forking process, used for debugging purposes
   Parameters - int pid - Process ID
   Returns - NONE
   Side Effects - NONE
   ----------------------------------------------------------------------- */
void p1_fork(int pid){
	if (DEBUG && debugflag)
		console("p1_fork() called: pid = %d\n", pid);
}

/* ------------------------------------------------------------------------
   Name - p1_switch
   Purpose - Placeholder for switching process, used for debugging purposes
   Parameters - int old - Old process ID
   	   	   	   	int new - New process ID
   Returns - NONE
   Side Effects - NONE
   ----------------------------------------------------------------------- */

void p1_switch(int old, int new){
	if (DEBUG && debugflag)
		console("p1_switch() called: old = %d, new = %d\n", old, new);
}

/* ------------------------------------------------------------------------
   Name - p1_quit
   Purpose - Placeholder for quitting process, used for debugging purposes
   Parameters - int pid - Process ID
   Returns - NONE
   Side Effects - NONE
   ----------------------------------------------------------------------- */

void p1_quit(int pid){
	if (DEBUG && debugflag)
		console("p1_quit() called: pid = %d\n", pid);
}

/* ------------------------------------------------------------------------
   Name - check_io
   Purpose - Placeholder
   Parameters - NONE
   Returns - INT
   Side Effects - NONE
   ----------------------------------------------------------------------- */

int check_io(){
	return 0;
}
