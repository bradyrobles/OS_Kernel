/* ------------------------------------------------------------------------
   phase2.c

   Brady Robles
   March 26, 2020
   CSCV 452 The Kernel Phase 2

   ------------------------------------------------------------------------ */


#include <phase2.h>
#include <phase1.h>
#include <usloss.h>
#include <string.h>
#include "message.h"


/* ------------------------- Prototypes ----------------------------------- */
int         start1 (char *);
extern int  start2 (char *);
void		check_kernel_mode(char * proc);
void		disableInterrupts();
void		enableInterrupts();
int 		check_io();

void		zeroMailbox(int mailbox_ID);
void		zeroSlot(int slot_ID);
void		zeroMboxSlot(int pid);


int			MboxRelease(int mailboxID);
int			MboxCondSend(int mailboxID, void *msg, int msg_Size);
int			MboxCondReceive(int mailboxID, void *msg, int max_msg_Size);

slot_ptr    createSlot(int index, int mbox_ID, void *msg, int msg_Size);
int			getSlot();
int			addSlotToList(slot_ptr slot, mail_box_ptr mailbox_ptr);



/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

/* the mail boxes and slot array */
mail_box MailBoxTable[MAXMBOX];
mail_slot SlotTable[MAXSLOTS];

// Process Table
mbox_proc MboxProcessTable[MAXPROC];


/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name         - start1
   Purpose 		- Initializes mailboxes and interrupt vector.
             	  Start the phase2 test process.
   Parameters 	- one, default arg passed by fork1, not used here.
   Returns 		- one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
   if (DEBUG2 && debugflag2)
      console("start1(): at beginning\n");

   int kid_pid;
   int status;

   check_kernel_mode("start1");

   /* Disable interrupts */
   disableInterrupts();

   /* Initialize the mail box table, slots, & other data structures.
    * Initialize int_vec and sys_vec, allocate mailboxes for interrupt
    * handlers.  Etc... */
   // Initialize the mail box table
   for (int i = 0; i < MAXMBOX; i++){
	   MailBoxTable[i].mbox_ID = i;
	   zeroMailbox(i);
   }
   // Initialize slot array
   for (int i = 0; i < MAXSLOTS; i++){
	   SlotTable[i].slot_ID = i;
	   zeroSlot(i);
   }

   // Initialize the process table
   for (int i = 0; i < MAXPROC; i++){
	   zeroMboxSlot(i);
   }

   enableInterrupts();

   /* Create a process for start2, then block on a join until start2 quits */
   if (DEBUG2 && debugflag2)
      console("start1(): fork'ing start2 process\n");
   kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
   if ( join(&status) != kid_pid ) {
      console("start2(): join returned something other than start2's pid\n");
   }

   return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
	check_kernel_mode("MboxCreate");
	disableInterrupts();

	// Check for available slots
	if (slots < 0) {
		enableInterrupts();
		return -1;
	}

	// Check for slot_size constraints
	if (slot_size < 0 || slot_size > MAX_MESSAGE){
		enableInterrupts();
		return -1;
	}

	// Create Mailbox in next available entry in MailboxTable
	for (int i = 0; i < MAXMBOX; i++){
		if (MailBoxTable[i].status == EMPTY){
			MailBoxTable[i].status = USED;
			MailBoxTable[i].num_Slots = slots;
			MailBoxTable[i].num_Slots_Used = 0;
			MailBoxTable[i].slot_Size = slot_size;
			enableInterrupts();
			return i;
		}
	}
	enableInterrupts();
	return -1;
} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
	check_kernel_mode("MboxSend");
	disableInterrupts();

	// Check if parameters are correct
	if (mbox_id > MAXMBOX || mbox_id < 0){
		enableInterrupts();
		return -1;
	}

	// Check if mailbox being sent message exists
	if (MailBoxTable[mbox_id].status == EMPTY){
		enableInterrupts();
		return -1;
	}

	mail_box_ptr ptr = &MailBoxTable[mbox_id];

	// Check if room in target mailbox for msg
	if (msg_size > ptr->slot_Size){
		enableInterrupts();
		return -1;
	}

	// add to process table
	int pid = getpid();
	MboxProcessTable[pid % MAXPROC].pid = pid;
	MboxProcessTable[pid % MAXPROC].status = ACTIVE;
	MboxProcessTable[pid % MAXPROC].msg = msg_ptr;
	MboxProcessTable[pid % MAXPROC].msg_Size = msg_size;

	// Block on no available slots and add to block send list
	if (ptr->num_Slots <= ptr->num_Slots_Used && ptr->block_Receive_List == NULL){

		// Placing process in blocked send list
		if (ptr->block_Send_List == NULL){
			ptr->block_Send_List = &MboxProcessTable[pid % MAXPROC];
		} else{
			mbox_proc_ptr temp = ptr->block_Send_List;
			while (temp->next_Block_Send != NULL){
				temp = temp->next_Block_Send;
			}
			temp->next_Block_Send = &MboxProcessTable[pid % MAXPROC];
		}
		block_me(SEND_BLOCK);

		// check if the process was released
		if(MboxProcessTable[pid % MAXPROC].mbox_Released){
			enableInterrupts();
			return -3;
		}
		return is_zapped() ? -3 : 0;
	}

	// Check if process on receive blocked list
	if (ptr->block_Receive_List != NULL){

		// message is bigger than receive size
		if (msg_size > ptr->block_Receive_List->msg_Size){
			ptr->block_Receive_List->status = FAILED;
			int pid = ptr->block_Receive_List->pid;
			ptr->block_Receive_List = ptr->block_Receive_List->next_Block_Receive;
			unblock_proc(pid);
			enableInterrupts();
			return -1;
		}

		// Copy message to receive buffer
		memcpy(ptr->block_Receive_List->msg, msg_ptr, msg_size);
		ptr->block_Receive_List->msg_Size = msg_size;

		int receivePID = ptr->block_Receive_List->pid;
		ptr->block_Receive_List = ptr->block_Receive_List->next_Block_Receive;
		unblock_proc(receivePID);
		enableInterrupts();
		return is_zapped() ? -3 : 0;
	}

	// find empty slot in SlotTable
	int slot = getSlot();
	if (slot == -2){
		console("MboxSend(): No slots available. Halting...\n");
		halt(1);
	}

	// Initialize and add slot to global slotlist
	slot_ptr new_slot = createSlot(slot, ptr->mbox_ID, msg_ptr, msg_size);

	// add slot to mailbox slotlist
	addSlotToList(new_slot, ptr);

	enableInterrupts();
	return is_zapped() ? -3 : 0;

} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int max_msg_size)
{
	check_kernel_mode("MboxReceive");
	disableInterrupts();

	// check that mailbox to receive exists
	if (MailBoxTable[mbox_id].status == EMPTY){
		enableInterrupts();
		return -1;
	}

	mail_box_ptr ptr = &MailBoxTable[mbox_id];

	// check that max_msg_size is valid
	if (max_msg_size < 0){
		enableInterrupts();
		return -1;
	}

	// add to process table
	int pid = getpid();
	MboxProcessTable[pid % MAXPROC].pid = pid;
	MboxProcessTable[pid % MAXPROC].status = ACTIVE;
	MboxProcessTable[pid % MAXPROC].msg = msg_ptr;
	MboxProcessTable[pid % MAXPROC].msg_Size = max_msg_size;

	slot_ptr slotPTR = ptr->slot_List;

	// No full slots for mailbox
	if (slotPTR == NULL){

		// add receive process to its own blocked receive list
		if (ptr->block_Receive_List == NULL){
			ptr->block_Receive_List = &MboxProcessTable[pid % MAXPROC];
		}else{
			mbox_proc_ptr temp = ptr->block_Receive_List;

			while (temp->next_Block_Receive != NULL){
				temp = temp->next_Block_Receive;
			}
			temp->next_Block_Receive = &MboxProcessTable[pid % MAXPROC];
		}

		// block until sender arrives
		block_me(RECEIVE_BLOCK);

		// Mailbox was released or zapped
		if(MboxProcessTable[pid % MAXPROC].mbox_Released || is_zapped()){
			enableInterrupts();
			return -3;
		}

		// Failed to receive message
		if (MboxProcessTable[pid % MAXPROC].status == FAILED){
			enableInterrupts();
			return -1;
		}
		enableInterrupts();
		return MboxProcessTable[pid % MAXPROC].msg_Size;

	// At least 1 full slot in mailbox
	}else{

		if (slotPTR->msg_Size > max_msg_size){
			enableInterrupts();
			return -1;
		}

		// copy message into receive
		memcpy(msg_ptr, slotPTR->msg, slotPTR->msg_Size);
		ptr->slot_List = slotPTR->next_Slot;
		int msgSize = slotPTR->msg_Size;

		// Zero slot and reduce number of used slots
		zeroSlot(slotPTR->slot_ID);
		ptr->num_Slots_Used --;

		// Mailbox has a message on blocked send list
		if (ptr->block_Send_List != NULL){

			int index = getSlot();

			// initialize slot and add it to mailbox slotlist
			slot_ptr new_slot = createSlot(index, ptr->mbox_ID, ptr->block_Send_List->msg, ptr->block_Send_List->msg_Size);
			addSlotToList(new_slot, ptr);

			// update process blocked
			int pid = ptr->block_Send_List->pid;
			ptr->block_Send_List = ptr->block_Send_List->next_Block_Send;
			unblock_proc(pid);
		}
		enableInterrupts();
		return is_zapped() ? -3 : msgSize;
	}
} /* MboxReceive */

/* ------------------------------------------------------------------------
   Name - MboxRelease
   Purpose - Release mailbox, alert any blocked processes waiting on mailbox
   Parameters - mailbox id
   Returns - 0 if successful release, -1 if mailbox to be released DNE, -3 if process zapped
   Side Effects - zeroes mailbox and alerts the blocked procs
   ----------------------------------------------------------------------- */
int MboxRelease(int mbox_ID){
	check_kernel_mode("MboxRelease");
	disableInterrupts();

	// check mbox ID is valid
	if (mbox_ID < 0 || mbox_ID >= MAXMBOX){
		enableInterrupts();
		return -1;
	}

	// check that mailBox is created
	if (MailBoxTable[mbox_ID].status == EMPTY){
		enableInterrupts();
		return -1;
	}

	mail_box_ptr ptr = &MailBoxTable[mbox_ID];

	//no blocked processes
	if (ptr->block_Receive_List == NULL && ptr->block_Send_List == NULL){
		zeroMailbox(mbox_ID);
		enableInterrupts();
		return is_zapped() ? -3 : 0;
	}else{
		ptr->status = EMPTY;

		// mark all blocked processes as released
		while (ptr->block_Send_List != NULL){
			ptr->block_Send_List->mbox_Released = 1;
			int pid = ptr->block_Send_List->pid;
			ptr->block_Send_List = ptr->block_Send_List->next_Block_Send;
			unblock_proc(pid);
			disableInterrupts();
		}
		while (ptr->block_Receive_List != NULL){
			ptr->block_Receive_List->mbox_Released = 1;
			int pid = ptr->block_Receive_List->pid;
			ptr->block_Receive_List = ptr->block_Receive_List->next_Block_Receive;
			unblock_proc(pid);
			disableInterrupts();
		}
	}
	zeroMailbox(mbox_ID);
	enableInterrupts();
	return is_zapped() ? -3 : 0;
} /* MboxRelease */

/* ------------------------------------------------------------------------
   Name - MboxCondSend
   Purpose - Put a message into a slot for the indicated mailbox.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args, -2 for full mailbox, -3 for zapped process
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxCondSend(int mbox_ID, void *msg_ptr, int msg_size){
	check_kernel_mode("MboxCondSend");
	disableInterrupts();

	// check if mbox id is valid
	if (mbox_ID > MAXMBOX || mbox_ID < 0){
		enableInterrupts();
		return -1;
	}

	mail_box_ptr ptr = &MailBoxTable[mbox_ID];

	// check that receive buffer is large enough
	if (msg_size > ptr->slot_Size){
			enableInterrupts();
			return -1;
		}

	// Add process to ProcTable
	int pid = getpid();
	MboxProcessTable[pid % MAXPROC].pid = pid;
	MboxProcessTable[pid % MAXPROC].status = ACTIVE;
	MboxProcessTable[pid % MAXPROC].msg = msg_ptr;
	MboxProcessTable[pid % MAXPROC].msg_Size = msg_size;

	// Check that there is an open slot in mailbox
	if (ptr->num_Slots == ptr->num_Slots_Used){
		return -2;
	}

	// check if process is on the receive blocked list
	if (ptr->block_Receive_List != NULL){
		if (msg_size > ptr->block_Receive_List->msg_Size){
			enableInterrupts();
			return -1;
		}

		// copy message into message buffer
		memcpy(ptr->block_Receive_List->msg, msg_ptr, msg_size);
		ptr->block_Receive_List->msg_Size = msg_size;
		int receivePID = ptr->block_Receive_List->pid;
		ptr->block_Receive_List = ptr->block_Receive_List->next_Block_Receive;
		unblock_proc(receivePID);
		enableInterrupts();
		return is_zapped() ? -3 : 0;
	}

	// Find an empty slot in Slot table
	int slot = getSlot();
	if (slot == -2){
		return -2;
	}

	// Initialize slot and add it to mailbox slot list
	slot_ptr new_slot = createSlot(slot, ptr->mbox_ID, msg_ptr, msg_size);
	addSlotToList(new_slot, ptr);

	enableInterrupts();
	return is_zapped() ? -3 : 0;
} /* MboxCondSend*/

/* ------------------------------------------------------------------------
   Name - MboxCondReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args, -2 if mailbox empty, -3 if zapped
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxCondReceive(int mbox_ID, void *msg_ptr, int msg_size){
	check_kernel_mode("MboxCondReceive");
	disableInterrupts();

	// check that mailbox to receive exists
	if (MailBoxTable[mbox_ID].status == EMPTY){
		enableInterrupts();
		return -1;
	}

	mail_box_ptr ptr = &MailBoxTable[mbox_ID];

	// check that msg size is valid
	if (msg_size < 0){
		enableInterrupts();
		return -1;
	}

	// Add process to Proc Table
	int pid = getpid();
	MboxProcessTable[pid % MAXPROC].pid = pid;
	MboxProcessTable[pid % MAXPROC].status = ACTIVE;
	MboxProcessTable[pid % MAXPROC].msg = msg_ptr;
	MboxProcessTable[pid % MAXPROC].msg_Size = msg_size;


	slot_ptr slotPTR = ptr->slot_List;

	// No message in mailbox slots
	if (slotPTR == NULL){
		enableInterrupts();
		return -2;

	} else{

		// check that message buffer is big enough
		if (slotPTR->msg_Size > msg_size){
			enableInterrupts();
			return -1;
		}

		// copy message into receive buffer
		memcpy(msg_ptr, slotPTR->msg, slotPTR->msg_Size);
		ptr->slot_List = slotPTR->next_Slot;
		int msg_Size = slotPTR->msg_Size;

		// zero slot and reduce number of used slots
		zeroSlot(slotPTR->slot_ID);
		ptr->num_Slots_Used--;

		// check if blocked message on send list
		if (ptr->block_Send_List != NULL){
			int slot = getSlot();

			// initialize slot and add it to mailbox slot
			slot_ptr new_slot = createSlot(slot, ptr->mbox_ID, ptr->block_Send_List->msg, ptr->block_Send_List->msg_Size);
			addSlotToList(new_slot, ptr);

			// update process and unblock
			int pid = ptr->block_Send_List->pid;
			ptr->block_Send_List = ptr->block_Send_List->next_Block_Send;
			unblock_proc(pid);
		}
		enableInterrupts();
		return is_zapped() ? -3: msg_Size;
	}
} /* MboxCondReceive */

/*
 * check_kernel_mode
 */
void check_kernel_mode(char * proc){
	if ((PSR_CURRENT_MODE & psr_get()) == 0){
		console("check_kernel_mode(): called while in user mode by process %s. Halting...\n", proc);
		halt(1);
	}
}

/*
 * enableInterrupts
 */
void enableInterrupts(){
	psr_set(psr_get() | PSR_CURRENT_INT);
}

/*
 * disableInterrupts
 */
void disableInterrupts(){
	psr_set(psr_get() & ~PSR_CURRENT_INT);
}

/*
 * Zero all elements of the mailbox id
 */
void zeroMailbox(int mbox_ID){
	MailBoxTable[mbox_ID].status = EMPTY;
	MailBoxTable[mbox_ID].block_Receive_List = NULL;
	MailBoxTable[mbox_ID].block_Send_List = NULL;
	MailBoxTable[mbox_ID].slot_List = NULL;
	MailBoxTable[mbox_ID].num_Slots = -1;
	MailBoxTable[mbox_ID].num_Slots_Used = -1;
	MailBoxTable[mbox_ID].slot_Size = -1;

}

/*
 * Zero all elements of the slot id
 */
void zeroSlot(int slot_ID){
	SlotTable[slot_ID].status = EMPTY;
	SlotTable[slot_ID].next_Slot = NULL;
	SlotTable[slot_ID].mbox_ID = -1;

}

/*
 * Zero all elements of the process id
 */
void zeroMboxSlot(int pid){
	MboxProcessTable[pid % MAXPROC].status = EMPTY;
	MboxProcessTable[pid % MAXPROC].msg = NULL;
	MboxProcessTable[pid % MAXPROC].next_Block_Receive = NULL;
	MboxProcessTable[pid % MAXPROC].next_Block_Send = NULL;
	MboxProcessTable[pid % MAXPROC].pid = -1;
	MboxProcessTable[pid % MAXPROC].msg_Size = -1;
	MboxProcessTable[pid % MAXPROC].mbox_Released = 0;
}

/*
 * Return the index of the next available slot, -2 if no available
 */
int getSlot(){
	for (int i = 0; i < MAXSLOTS; i++){
		if (SlotTable[i].status == EMPTY){
			return i;
		}
	}
	return -2;
}

/*
 * Intialize a new slot in the slottable
 */
slot_ptr createSlot(int index, int mbox_ID, void *msg, int msg_size){
	SlotTable[index].mbox_ID = mbox_ID;
	SlotTable[index].status = USED;
	memcpy(SlotTable[index].msg, msg, msg_size);
	SlotTable[index].msg_Size = msg_size;
	return &SlotTable[index];
}

int check_io(){
	return 0;
}

/*
 * Add a slot to the slot list
 */
int addSlotToList(slot_ptr new_slot, mail_box_ptr ptr){
	slot_ptr head = ptr->slot_List;
	if (head == NULL){
		ptr->slot_List = new_slot;
	}else{
		while (head->next_Slot != NULL){
			head = head->next_Slot;
		}
		head->next_Slot = new_slot;
	}
	return ptr->num_Slots_Used++;
}

