/* ------------------------------------------------------------------------
   message.h

   Brady Robles
   March 26, 2020
   CSCV 452 The Kernel Phase 2

   ------------------------------------------------------------------------ */
#define DEBUG2 0

#define EMPTY  0
#define USED   1
#define ACTIVE 2
#define FAILED 3

#define SEND_BLOCK 101
#define RECEIVE_BLOCK 102

typedef struct mailbox mail_box;
typedef struct mailbox *mail_box_ptr;

typedef struct mbox_proc mbox_proc;
typedef struct mbox_proc *mbox_proc_ptr;

typedef struct mail_slot mail_slot;
typedef struct mail_slot *slot_ptr;

struct mbox_proc {
	short		 pid;
	int          status;
	void        *msg;
	int          msg_Size;
	int			 mbox_Released;
	mbox_proc_ptr next_Block_Send;
	mbox_proc_ptr next_Block_Receive;
};


struct mailbox {
	int			 mbox_ID;
	int			 status;
	int			 num_Slots;
	int			 num_Slots_Used;
	int          slot_Size;
	mbox_proc_ptr block_Send_List;
	mbox_proc_ptr block_Receive_List;
	slot_ptr     slot_List;
};

struct mail_slot {
	int          slot_ID;
	int		     mbox_ID;
	int          status;
	char         msg[MAX_MESSAGE];
	int          msg_Size;
	slot_ptr     next_Slot;
};

struct psr_bits {
    unsigned int cur_mode:1;
    unsigned int cur_int_enable:1;
    unsigned int prev_mode:1;
    unsigned int prev_int_enable:1;
    unsigned int unused:28;
};

union psr_values {
   struct psr_bits bits;
   unsigned int integer_part;
};
