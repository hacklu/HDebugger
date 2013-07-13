/*
 * 	Debugger Demo in GNU/Hurd 
 * 	write by hacklu @2013
 *
 */
#define _GNU_SOURCE 1

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h> //for exec..
#include <sys/ptrace.h>
#include <errno.h>
#include <mach.h>
#include <mach_error.h>
#include <mach/notify.h> //for MACH_NOTIFY_DEAD_NAME
#include <mach/i386/thread_status.h>
#include <string.h> //for memset

#include "gdb_obstack.h"

#define THREAD_STATE_FLAVOR i386_REGS_SEGS_STATE
#define DEBUG_ON 2
static int debug_level = DEBUG_ON;


void my_wait(mach_port_t port, pid_t child_pid)
{
	struct msg
	{
		mach_msg_header_t hdr;
		mach_msg_type_t type;
		int data[8000];
	}msg;
	error_t err;
	err = proc_wait_request(getproc(), port,child_pid,2);
	if(err)
		printf("error: proc_wait_request %d\n",err);
	printf("waiting for an event...\n");
	err = mach_msg(&msg.hdr,MACH_RCV_MSG | MACH_RCV_INTERRUPT,0,sizeof(struct msg), port,1000, MACH_PORT_NULL);
	printf("get event msg id = %d\n",msg.hdr.msgh_id);
}

void my_resume(thread_array_t threads, mach_msg_type_number_t num_thread)
{
	int i;
	if(debug_level>0)
		printf("my_resume %d threads \n",num_thread);
	for(i=0;i<num_thread;i++)
		thread_resume(threads[i]);
}	

thread_array_t update_proc(mach_port_t task_port, mach_port_t event_port,thread_array_t old, mach_msg_type_number_t* old_num)
{
	error_t err;
	thread_array_t threads;
	mach_msg_type_number_t num_threads=0,i;
	thread_state_t state;
	mach_port_t prev_port;

	if(debug_level>0)
		printf("update proc \n");
	err = task_threads(task_port,&threads,&num_threads);
	if(err)
		printf("error: task_threads %d\n",err);
	/*printf("get %d threads\n",num_threads);*/
	int *match = NULL;
	match = malloc(sizeof(int) * (int)num_threads);
	memset(match,0,sizeof(int) * (int)num_threads);
	int j,left;
	for(j=0;j<*old_num;j++){
		left = num_threads;
		for(i=0;i<num_threads;i++,left--){
			if(old[j] == threads[i]){
				match[i] = 1;
				mach_port_deallocate(mach_task_self(), old[j]);
				break;
			}
		}
		if(!left){ //thread died
			printf("died thread [%d]\n",old[j]);
		}
	}
	for(i=0;i< num_threads;i++){
		if(match[i]==0){
			printf("new thread [%d]\n",threads[i]);
			/*printf("new thread [%d]\n",threads[i]);*/
			err = thread_set_exception_port(threads[i],event_port);
			if(err)
				printf("error: thread_set_exception_port\n");
			err = mach_port_request_notification(mach_task_self(), threads[i], MACH_NOTIFY_DEAD_NAME, 1, event_port, MACH_MSG_TYPE_MAKE_SEND_ONCE,&prev_port);
			if(err)
				printf("error: thread_set_exception_port\n");
			else {
				if(prev_port != MACH_PORT_NULL)
					mach_port_deallocate(mach_task_self(), prev_port);
			}
		}

	}
	*old_num = num_threads;
	return threads;
}

int my_vm_read(mach_port_t task, unsigned long addr, char *data, int length)
{
	error_t err;
	vm_address_t low_address = (vm_address_t) trunc_page (addr);
	vm_size_t aligned_length =
		(vm_size_t) round_page (addr + length) - low_address;
	pointer_t copied;
	int copy_count;

	if(debug_level>1)
		printf("in my_vm_read(): low_address=%x, aligned_length=%d\n",low_address,aligned_length);
	/* Get memory from inferior with page aligned addresses.  */
	err = vm_read (task, low_address, aligned_length, &copied, &copy_count);
	if (err)
		return 0;

	err = hurd_safe_copyin (data, (void *) (addr - low_address + copied),
			length);
	if (err)
	{
		printf("Read from inferior faulted: %d\n",err);
		length = 0;
	}

	err = vm_deallocate (mach_task_self(), copied, copy_count);
	if (err)
		printf("my_vm_read deallocate failed. err=%d\n",err);

	return length;
}

#define CHK_GOTO_OUT(str,ret) \
  do if (ret != KERN_SUCCESS) { errstr = #str; goto out; } while(0)

struct vm_region_list
{
  struct vm_region_list *next;
  vm_prot_t protection;
  vm_address_t start;
  vm_size_t length;
};

int my_vm_write(mach_port_t task, unsigned long addr, char *data, int length)
{
  error_t err = 0;
  vm_address_t low_address = (vm_address_t) trunc_page (addr);
  vm_size_t aligned_length =
  (vm_size_t) round_page (addr + length) - low_address;
  pointer_t copied;
  int copy_count;
  int deallocate = 0;

  char *errstr = "Bug in my_vm_write";

  struct vm_region_list *region_element;
  struct vm_region_list *region_head = (struct vm_region_list *) NULL;

  /* Get memory from inferior with page aligned addresses.  */
  err = vm_read (task,
		 low_address,
		 aligned_length,
		 &copied,
		 &copy_count);
  CHK_GOTO_OUT ("gnu_write_inferior vm_read failed", err);

  deallocate++;

  err = hurd_safe_copyout ((void *) (addr - low_address + copied),
			   data, length);
  CHK_GOTO_OUT ("Write to inferior faulted", err);


  /* Do writes atomically.
     First check for holes and unwritable memory.  */
  {
    vm_size_t remaining_length = aligned_length;
    vm_address_t region_address = low_address;

    struct vm_region_list *scan;

    while (region_address < low_address + aligned_length)
      {
	vm_prot_t protection;
	vm_prot_t max_protection;
	vm_inherit_t inheritance;
	boolean_t shared;
	mach_port_t object_name;
	vm_offset_t offset;
	vm_size_t region_length = remaining_length;
	vm_address_t old_address = region_address;

	err = vm_region (task,
			 &region_address,
			 &region_length,
			 &protection,
			 &max_protection,
			 &inheritance,
			 &shared,
			 &object_name,
			 &offset);
	CHK_GOTO_OUT ("vm_region failed", err);

	/* Check for holes in memory.  */
	if (old_address != region_address)
	  {
	    printf ("No memory at 0x%x. Nothing written",
		     old_address);
	    err = KERN_SUCCESS;
	    length = 0;
	    goto out;
	  }

	if (!(max_protection & VM_PROT_WRITE))
	  {
	    printf("Memory at address 0x%x is unwritable. Nothing written\n",
		     old_address);
	    err = KERN_SUCCESS;
	    length = 0;
	    goto out;
	  }

	/* Chain the regions for later use.  */
	region_element =
	  (struct vm_region_list *)
	  malloc(sizeof (struct vm_region_list));

	region_element->protection = protection;
	region_element->start = region_address;
	region_element->length = region_length;

	/* Chain the regions along with protections.  */
	region_element->next = region_head;
	region_head = region_element;

	region_address += region_length;
	remaining_length = remaining_length - region_length;
      }

    /* If things fail after this, we give up.
       Somebody is messing up inferior_task's mappings.  */

    /* Enable writes to the chained vm regions.  */
    for (scan = region_head; scan; scan = scan->next)
      {
	if (!(scan->protection & VM_PROT_WRITE))
	  {
	    err = vm_protect (task,
			      scan->start,
			      scan->length,
			      FALSE,
			      scan->protection | VM_PROT_WRITE);
	    CHK_GOTO_OUT ("vm_protect: enable write failed", err);
	  }
      }

    err = vm_write (task,
		    low_address,
		    copied,
		    aligned_length);
    CHK_GOTO_OUT ("vm_write failed", err);

    /* Set up the original region protections, if they were changed.  */
    for (scan = region_head; scan; scan = scan->next)
      {
	if (!(scan->protection & VM_PROT_WRITE))
	  {
	    err = vm_protect (task,
			      scan->start,
			      scan->length,
			      FALSE,
			      scan->protection);
	    CHK_GOTO_OUT ("vm_protect: enable write failed", err);
	  }
      }
  }

out:
  if (deallocate)
    {
      (void) vm_deallocate (mach_task_self (),
			    copied,
			    copy_count);
    }

  if (err != KERN_SUCCESS)
    {
      printf("%s: %s", errstr, mach_error_string (err));
      return 0;
    }

  return length;
}

void run_debugger(pid_t child_pid)
{
	/*printf("father says:%d\n",getpid());*/
	struct msg
	{
		mach_msg_header_t hdr;
		mach_msg_type_t type;
		int data[8000];
	}msg;
	int wait_status;
	unsigned iconuter = 0;
	error_t err;
	mach_port_t port;
	mach_port_t prev_port=MACH_PORT_NULL;
	task_t task_port;
	thread_array_t threads=0;
	thread_array_t threads_tmp=0;
	mach_msg_type_number_t num_threads=0,i;
	thread_state_t state;
	mach_msg_type_number_t state_size = i386_THREAD_STATE_COUNT;

	err = mach_port_allocate(mach_task_self(),
				MACH_PORT_RIGHT_RECEIVE, &port);
	if(err)
		printf("error: allocating event port\n");
		/*printf("error: allocating event port:%s",safe_strerror(err));*/

	mach_port_insert_right(mach_task_self(),
				port,port,MACH_MSG_TYPE_MAKE_SEND);
	err = proc_pid2task(getproc(),child_pid,&task_port);
	if(err)
		printf("error: proc_pid2task\n");

	err = task_set_exception_port(task_port,port);
	if(err)
		printf("error: task_set_exception_port\n");

	if(task_port){
		threads = update_proc(task_port,port,threads,&num_threads);
		my_wait(port, child_pid);
	}
		threads = update_proc(task_port,port,threads,&num_threads);
		my_wait(port, child_pid);
		
		threads = update_proc(task_port,port,threads,&num_threads);
		my_wait(port, child_pid);

		threads = update_proc(task_port,port,threads,&num_threads);
	err = proc_wait_request(getproc(), port,child_pid,2);
	if(err)
		printf("err=%d\n",err);


	//read memory from inferior
	char buf[1024];
	int read_count=0;
	int length=32;
	int addr=0x80484ec;
	char breakpoint=0xcc; //int 3
	read_count = my_vm_read(task_port,addr,buf,length);
	if(read_count){
		printf("read count=%d bytes\n",read_count);
		printf("[0x%x]: ",addr);
		for(i=0;i<read_count;i++)
			printf("%02x ",buf[i]& 0xFF);
		printf("\n");

	}

	err = my_vm_write(task_port,0x80484f9,&breakpoint,1);
	if(err==0)
		printf("my_vm_write error; write %d bytes\n",err);

	read_count = my_vm_read(task_port,addr,buf,length);
	if(read_count){
		printf("read count=%d bytes\n",read_count);
		printf("[0x%x]: ",addr);
		for(i=0;i<read_count;i++)
			printf("%02x ",buf[i]& 0xFF);
		printf("\n");

	}

/*exit(0);	*/

	my_resume(threads,num_threads);

	printf("debugger exit in 3s..\n");
	sleep(3);
}

void run_target(const char* programname)
{
	printf("[inferior] pid=%d\n",getpid());
	if(ptrace(PTRACE_TRACEME)!=0){
		printf("[inferior]error in trace me!\n");
		}
	printf("[inferior]target after trace_me\n");
	printf("[inferior]target before execl\n");
	/*sleep(10);*/
	execl(programname,programname,0);
	printf("[inferior]target after xecl\n"); //you will never see this..
}

void * just_print(void *arg)
{
	int i=0;
	while(1){
		sleep(1); //here, If put this line behind printf(), the program will hang
		printf("===========just_print say %d\n",i++);
	}
}
int main(int argc,char** argv)
{
	pthread_t thread_print;
	if(argc<2){
		printf("Usage: %s debugged\n",argv[0]);
		exit(-1);
	}

	pthread_create( &thread_print,NULL,just_print,(void*)NULL);
	/*sleep(1);*/ //if add this, the world change!!! don't know why

	int pid=0;
	pid = fork();
	if(pid ==0){ // child process
		run_target(argv[1]);
	}
	else {	// father process
		/*sleep(100);*/
		/*sleep(2);*/
		run_debugger(pid);
	}
	return 0;
}
