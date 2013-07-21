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
#include <pthread.h>
#include <hurd/signal.h>
#include <mach/mig_errors.h>

#include "exc_request_S.h"
#include "msg_reply_S.h"

#define THREAD_STATE_FLAVOR i386_REGS_SEGS_STATE
#define DEBUG_ON 0
static int debug_level = DEBUG_ON;

struct msg_t
{
	mach_msg_header_t hdr;
	mach_msg_type_t type;
	int data[8000];
}msg;

#if 0 
struct i386_thread_state {  
        unsigned int    gs; 
        unsigned int    fs; 
        unsigned int    es; 
        unsigned int    ds; 
        unsigned int    edi;
        unsigned int    esi;
        unsigned int    ebp;
        unsigned int    esp;
        unsigned int    ebx;
        unsigned int    edx;
        unsigned int    ecx;
        unsigned int    eax;
        unsigned int    eip;
        unsigned int    cs; 
        unsigned int    efl;
        unsigned int    uesp
        unsigned int    ss; 
};                          
#endif

error_t do_wait(mach_port_t port, struct msg_t* mymsg)
{
	error_t err;
	if(debug_level>0)
		printf("waiting for an event...\n");
	err = mach_msg(&(mymsg->hdr),MACH_RCV_MSG | MACH_RCV_INTERRUPT,0,sizeof(struct msg_t), port,MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	if(debug_level>0)
		printf("get event msg id = %d\n",mymsg->hdr.msgh_id);
	return err;
}

void my_wait(mach_port_t port, pid_t child_pid)
{
	error_t err;
	err = proc_wait_request(getproc(), port,child_pid,2);
	if(err)
		printf("error: proc_wait_request %d\n",err);
	do_wait(port,&msg);
}

void my_resume(thread_array_t threads, mach_msg_type_number_t num_threads)
{
	int i;
	if(debug_level>0)
		printf("my_resume %d threads \n",num_threads);
	for(i=0;i<num_threads;i++)
		thread_resume(threads[i]);
}	

void my_suspend(thread_array_t threads, mach_msg_type_number_t num_threads)
{
	int i;
	if(debug_level>0)
		printf("my_suspend %d threads \n",num_threads);
	for(i=0;i<num_threads;i++)
		thread_suspend(threads[i]);
}	

void my_signal(pid_t pid, mach_port_t task_port, mach_port_t event_port, int sig)
{
  error_t err = 0;
  int host_sig = sig;
  if(sig==0){
      err =
	HURD_MSGPORT_RPC(proc_getmsgport(getproc(),pid,&msgport),
			(refport=task_port,0),
			0,
			msgport?(msg_sig_post_untraced_request (msgport,
							event_port,
					       MACH_MSG_TYPE_MAKE_SEND_ONCE,
							host_sig, 0,
							refport)):EIEIO);
    }

  if (err)
	  printf("my_signal err=%d\n",err);
}

thread_array_t update_proc(mach_port_t task_port, mach_port_t event_port,thread_array_t old, mach_msg_type_number_t* old_num)
{
	error_t err;
	thread_array_t threads;
	mach_msg_type_number_t num_threads=0,i;
	mach_port_t prev_port;

	if(debug_level>0)
		printf("update proc \n");
	err = task_threads(task_port,&threads,&num_threads);
	if(err)
		printf("error: task_threads %d\n",err);
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
			if(debug_level>0)
				printf("died thread [%d]\n",(int)old[j]);
		}
	}
	for(i=0;i< num_threads;i++){
		if(match[i]==0){
			if(debug_level>0)
				printf("new thread [%d]\n",(int)threads[i]);
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
	if(debug_level>0)
		printf("now has %d threads\n",num_threads);
	if(debug_level>0)
		printf("in update() threads=%p\n",threads);
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
		printf("(my_vm_read: task=%d, addr=0x%08lx, low_address=0x%08lx, length=%d, aligned_length=%d)\n",(int)task,addr,(unsigned long)low_address,length,aligned_length);
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

  if(debug_level>1)
	printf("(my_vm_write: task=%d, addr=0x%08x, length=%d)\n",task,addr,length);

  struct vm_region_list *region_element;
  struct vm_region_list *region_head = (struct vm_region_list *) NULL;

  /* Get memory from inferior with page aligned addresses.  */
  err = vm_read (task,
		 low_address,
		 aligned_length,
		 &copied,
		 &copy_count);
  CHK_GOTO_OUT ("my_vm_write vm_read failed", err);

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
		     (unsigned int)old_address);
	    err = KERN_SUCCESS;
	    length = 0;
	    goto out;
	  }

	if (!(max_protection & VM_PROT_WRITE))
	  {
	    printf("Memory at address 0x%x is unwritable. Nothing written\n",
		     (unsigned int)old_address);
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

void my_dump_mem(mach_port_t task_port, unsigned int addr, int length)
{
	char buf[1024];
	int read_count=0;
	int i;
	read_count = my_vm_read(task_port,addr,buf,length);
	if(read_count){
		printf("read count=%d bytes\n",read_count);
		printf("[0x%x]: ",addr);
		for(i=0;i<read_count;i++)
			printf("%02x ",buf[i]& 0xFF);
		printf("\n");
	}
}

//return 0; success
//otherwise fali.
//
int set_breakpoint(mach_port_t task_port, unsigned long addr, char *old_value)
{
	int read_count=0;
	char breakpoint=0xcc; //int 3
	error_t err;
	read_count = my_vm_read(task_port,addr,old_value,1);
	if(read_count!=1)
		return -1;

	err = my_vm_write(task_port,addr,&breakpoint,1);
	if(err==0){
		if(debug_level>0)
			printf("my_vm_write error; write %d bytes\n",err);
		return -1;
	}
	return 0;
}

int remove_breakpoint(mach_port_t task_port, unsigned long addr, char old_value)
{
	error_t err;
	err = my_vm_write(task_port,addr,&old_value,1);
	if(err==0){
		if(debug_level>0)
			printf("my_vm_write error; write %d bytes\n",err);
		return -1;
	}
	return 0;
}

error_t
S_exception_raise_request (mach_port_t port, mach_port_t reply_port,
			   thread_t thread_port, task_t task_port,
			   int exception, int code, int subcode)
{

	if(debug_level>0)
		printf("thread = %d, task = %d, exc = %d, code = %d, subcode = %d\n",(int)thread_port, (int)task_port, exception, code, subcode);
	return 0;
}

error_t S_msg_sig_post_untraced_reply (mach_port_t reply_port, kern_return_t return_code)
{
	if(debug_level>0)
		printf("in msg_sig_post_untraced_request_reply() reply_port=%d, return_code=%d\n",reply_port,return_code);
	return 0;
}

error_t S_msg_sig_post_reply (mach_port_t reply_port, kern_return_t return_code)
{
	return 0;
}

void my_thread_info(mach_port_t thread)
{
	thread_basic_info_data_t _info;
	thread_basic_info_t info= &_info;
	mach_msg_type_number_t info_len= THREAD_BASIC_INFO_COUNT;
	error_t err;

	err =thread_info (thread, THREAD_BASIC_INFO, (int *) info, &info_len);
	if (err)
		printf("get thread_info() error=%d\n",err);
	if(debug_level>0){
		printf("thread [%d] suspend_count=%d\n",thread,(&_info)->suspend_count);
		printf("run_state=%d, flags=%d, sleep_time=%d, creation_time.second=%d\n",info->run_state,info->flags,info->sleep_time,info->creation_time.seconds);
	}
}

void my_task_info(mach_port_t task)
{
	task_basic_info_data_t _info;
	task_basic_info_t info= &_info;
	mach_msg_type_number_t info_len= TASK_BASIC_INFO_COUNT;
	error_t err;

	err =task_info (task, TASK_BASIC_INFO, (int *) info, &info_len);
	if (err)
		printf("get task_info() error=%d\n",err);
	if(debug_level>0){
		printf("task [%d] suspend_count=%d\n",task,(&_info)->suspend_count);
		printf("creation_time.second=%d\n",info->creation_time.seconds);
	}

}
void run_debugger(pid_t child_pid)
{
	/*printf("father says:%d\n",getpid());*/
	error_t err;
	mach_port_t event_port;
	task_t task_port;
	thread_array_t threads=0;
	mach_msg_type_number_t num_threads=0,i;
	thread_state_data_t state;
	mach_msg_type_number_t state_size = i386_THREAD_STATE_COUNT;

	err = mach_port_allocate(mach_task_self(),
			MACH_PORT_RIGHT_RECEIVE, &event_port);
	if(err)
		printf("error: allocating event port\n");

	mach_port_insert_right(mach_task_self(),
			event_port,event_port,MACH_MSG_TYPE_MAKE_SEND);
	err = proc_pid2task(getproc(),child_pid,&task_port);
	if(err)
		printf("error: proc_pid2task\n");

	err = task_set_exception_port(task_port,event_port);
	if(err)
		printf("error: task_set_exception_port\n");

	if(!task_port){
		printf("error: task_port is NULL\n");
		exit(-1);
	}

	//the three my_wait to check the thread 1 thread 2 die, and new thread appears
	threads = update_proc(task_port,event_port,threads,&num_threads);
	my_wait(event_port, child_pid);

	threads = update_proc(task_port,event_port,threads,&num_threads);
	my_wait(event_port, child_pid);

	threads = update_proc(task_port,event_port,threads,&num_threads);
	my_wait(event_port, child_pid);

	if(err)
		printf("err=%d\n",err);

	//set breakpiont
	int addr=0x8048510; //the second printf();get from objdump -d
	char old_value;

	err=set_breakpoint(task_port,addr,&old_value);
	/*err=set_breakpoint(task_port,0x804851c,&old_value);*/
	if(err)
		printf("set breakpiont fail\n");
	/*addr=0x804854d; //jump the second printf(), bypass icache*/

	while(!threads) {
		/*sleep(1); //sometimes, if we call update_proc() to quick, the inferior hasn't create new thread, we will get NULL,so we need a little slower*/
		threads = update_proc(task_port,event_port,threads,&num_threads);
	}
	my_thread_info(threads[0]);

	//begin to run (after execl())
	my_resume(threads,num_threads);

	do_wait(event_port,&msg);
	threads = update_proc(task_port,event_port,threads,&num_threads);

	if(msg.hdr.msgh_id==2400) //handle int3 !!
	{
		my_thread_info(threads[0]);
		my_task_info(task_port);
		/*my_suspend(threads,num_threads);*/
		my_thread_info(threads[0]);
		my_thread_info(threads[0]);
		struct
		{
			mach_msg_header_t hdr;
			mach_msg_type_t err_type;
			kern_return_t err;
			char noise[200];
		}reply;
		exc_server(&msg.hdr, &reply.hdr);

		threads = update_proc(task_port,event_port,threads,&num_threads);
		if(debug_level>0)
			for(i=0;i<num_threads;i++)
				printf("threads[%d]=%d\n",i,(int)threads[i]);
		/*printf("sizeof thread_state_t = %d\n",sizeof(thread_state_t));*/
		err = thread_get_state (threads[0], THREAD_STATE_FLAVOR,(thread_state_t) &state, &state_size);
		if(err)
			printf("thread_get_state err=%d\n",err);
		if(debug_level>1){
			for(i=0;i<state_size;i++)
			printf("%08x ",*((int*)&state+i));
			printf("\n");
		}
		printf(">>>>>inferior hit breakpoint! at ");
		printf(" eip=0x%08x\n",((struct i386_thread_state*)&state)->eip);

		printf("please input enter key to contine the inferiror:)\n");
		getchar();
		//recovery
		{
			/*printf("task_port=%d, addr=0x%08x,old_value=0x%02x\n",(int)task_port,addr, old_value&0xff);*/
			err = remove_breakpoint(task_port,addr,old_value);
			if(err){
				printf("remove_breakpoint error\n");
				exit(0);
			}
			((struct i386_thread_state*)&state)->eip--;
			/*((struct i386_thread_state*)&state)->eip=0x804854d; //bypass breakpoint*/
			err = thread_set_state (threads[0], THREAD_STATE_FLAVOR,(thread_state_t) &state, i386_THREAD_STATE_COUNT);
			if(err)
				printf("thread_set_state err=%d\n",err);

			/*threads = update_proc(task_port,event_port,threads,&num_threads);*/
			my_signal(child_pid,task_port,event_port,0);

			do_wait(event_port,&msg);
			task_suspend(task_port);
			my_thread_info(threads[0]);
			my_task_info(task_port);

			msg_reply_server(&msg.hdr, &reply.hdr);

			thread_abort(threads[0]);  //this is the continue magic!!
			task_resume(task_port);

		}
	}

	if(debug_level>0){ //check exit message
		my_wait(event_port, child_pid);
		my_wait(event_port, child_pid);
		do_wait(event_port,&msg);
		do_wait(event_port,&msg);
		do_wait(event_port,&msg);
	}

	printf("debugger will exit in 3s..\n");
	int exit_time=3;
	while(exit_time--){
		sleep(1);
		printf(".");
		fflush(stdout);
	}
	printf("\n");
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
	execl(programname,programname,NULL);
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

	/*pthread_create( &thread_print,NULL,just_print,(void*)NULL);*/
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
