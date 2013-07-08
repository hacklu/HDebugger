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

#define THREAD_STATE_FLAVOR i386_REGS_SEGS_STATE
#define DEBUG_ON 1
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
		threads = update_proc(task_port,port,threads,&num_threads);
		threads = update_proc(task_port,port,threads,&num_threads);
		threads = update_proc(task_port,port,threads,&num_threads);
		my_resume(threads,num_threads);

	printf("debugger exit in 3s..\n");
	sleep(3);
}

void run_target(const char* programname)
{
	//printf("son says:%d\n",getpid());
	if(ptrace(PTRACE_TRACEME)!=0){
		printf("[inferior]error in trace me!\n");
		}
	printf("[inferior]target after trace_me\n");
	printf("[inferior]target before execl\n");
	execl(programname,programname,0);
	printf("[inferior]target after xecl\n"); //you will never see this..
}

void * just_print(void *arg)
{
	int i=0;
	while(1){
		printf("===========just_print say %d\n",i++);
		sleep(1);
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
		run_debugger(pid);
	}
	return 0;
}
