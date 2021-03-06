/* DISKTEST
 * - note that the test script should clean out the disk file
 * each time before running this test.
 * Write three sectors to the disk and then read them back.
 * Write starting at track 4, sector 15 -- should wrap around to track 5
 */

#include <stdio.h>
#include <usloss.h>
#include <libuser.h>
#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <strings.h>


static char sectors[3 * 512];
static char copy[3 * 512];
int start4(char *arg)
{
   int result;
   int status;

   printf("start4(): Writing data to 3 disk sectors, wrapping to next track\n");

   printf("\nstart4(): Disk 0:\n");
   strcpy(&sectors[0 * 512], "This is a test\n");
   strcpy(&sectors[1 * 512], "Does it work?\n");
   strcpy(&sectors[2 * 512], "One last chance\n");
   result = DiskWrite((char *) sectors, 0, 4, 15, 3, &status);
   assert(result == 0);
   result = DiskRead((char *) copy, 0, 4, 15, 3, &status);
   printf("start4(): Read from disk: %s\n", &copy[0*512]);
   printf("start4(): Read from disk: %s\n", &copy[1*512]);
   printf("start4(): Read from disk: %s\n", &copy[2*512]);

   printf("\nstart4(): Disk 1:\n");
   strcpy(&sectors[0 * 512], "This is a test\n");
   strcpy(&sectors[1 * 512], "Does it work?\n");
   strcpy(&sectors[2 * 512], "One last chance\n");
   result = DiskWrite((char *) sectors, 1, 4, 15, 3, &status);
   assert(result == 0);
   result = DiskRead((char *) copy, 1, 4, 15, 3, &status);
   printf("start4(): Read from disk: %s\n", &copy[0*512]);
   printf("start4(): Read from disk: %s\n", &copy[1*512]);
   printf("start4(): Read from disk: %s\n", &copy[2*512]);

   Terminate(24);
   return 0;
}

