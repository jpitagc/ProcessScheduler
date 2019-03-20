#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdbool.h>
#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler(); // que proceso es el siguiente e ser ejecutado 
void activator(); //   cambio de contexto 
void timer_interrupt(int sig);  // interrupcion de reloj  (tratamiento)
void disk_interrupt(int sig);   // interrupcion de disco   (tratamiento)

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];  

/* Current running thread */
static TCB* running;
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;
static bool expulsion=false;
static bool disk_expulsion= false;

struct queue * WAIT;
struct queue * HP;
struct queue * LP;

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function(){
  while(1);
}

/* Initialize the thread library */
void init_mythreadlib() {
  int i;  
  LP= queue_new();
  HP= queue_new();
  WAIT= queue_new();
  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }
  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;
  if(idle.run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;
  makecontext(&idle.run_env, idle_function, 1); 

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if(getcontext(&t_state[0].run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }	

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
 
  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */ 
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;
  
  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1); 

printf(" THREAD %i CREATED\n",i);
if(priority == HIGH_PRIORITY){
  t_state[i].ticks = -1;
  disable_interrupt();
  disable_disk_interrupt();
  enqueue(HP, &t_state[i]);
  enable_disk_interrupt();
  enable_interrupt();

}else if (priority == LOW_PRIORITY){
  t_state[i].ticks = QUANTUM_TICKS;
  disable_interrupt();
  disable_disk_interrupt();
  enqueue(LP, &t_state[i]);
  enable_disk_interrupt();
  enable_interrupt();

}else{
  printf("ERROR: Priority of thead neither high neither low\n");
  exit(-1);
}

  return i;
} /****** End my_thread_create() ******/

/* Read disk syscall */
int read_disk()
{
   if(data_in_page_cache()!=0){
      disable_interrupt();
      disable_disk_interrupt();
      enqueue(WAIT,running);
      enable_disk_interrupt();
      enable_interrupt();
      printf("*** THREAD %i READ FROM DISK\n",running->tid);
      disk_expulsion=true;
      activator(scheduler());
      disk_expulsion=false;
   }
   return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{

  disable_interrupt();
  disable_disk_interrupt();
  if(!queue_empty(WAIT)){
    TCB * ready = dequeue(WAIT);
    if(ready->priority == HIGH_PRIORITY ){
      enqueue(HP,ready);
    }else if(ready->priority == LOW_PRIORITY){
      enqueue(LP,ready);
    }else{
      printf("Unexpected Process in Waiting Queue... Exiting due to error\n");
      enable_disk_interrupt();
      enable_interrupt();
      exit(-1);
    }
    enable_disk_interrupt();
    enable_interrupt();
    printf("*** THREAD %i READY\n",ready->tid);

  }else{
    enable_disk_interrupt();
    enable_interrupt();
  }
} 


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	

  printf("*** THREAD %d FINISHED\n", tid);	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 

  TCB* next = scheduler();
  activator(next);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}


/* FIFO para alta prioridad, RR para baja*/
TCB* scheduler(){

/*
  printf("\nCola De Alta Prioridad ");
  queue_print(HP);
  printf("\nCola De Baja Prioridad ");
  queue_print(LP);
  printf("\nCola De Espera ");
  queue_print(WAIT);
  printf("\n");*/

 TCB* proceso;
  disable_interrupt();
  disable_disk_interrupt();
  if(!queue_empty(HP)){
    proceso= dequeue(HP);
  }
  else if (!queue_empty(LP)){
    proceso = dequeue(LP);
  }
  else if(!queue_empty(WAIT)){
    proceso = &idle;
  }
  else{
    printf("*** FINISH \n");
    enable_disk_interrupt();
    enable_interrupt();
    exit(1);
  }
  enable_disk_interrupt();
  enable_interrupt();

 
  return proceso;


  printf("mythread_free: No thread in the system\nExiting...\n"); 
  exit(1);  
}


/* Timer interrupt  */
void timer_interrupt(int sig)
{
   if(running->priority==HIGH_PRIORITY){
    if(running->state == FREE){
      activator(scheduler());
    }
    return;
    
  }
  else if (running->priority == LOW_PRIORITY){
     

    if(running->state == FREE){
        activator(scheduler());
        return;
    }
    

    running->ticks--;
    disable_interrupt();
    disable_disk_interrupt();
    if(!queue_empty(HP)){
      
      enable_disk_interrupt();
      enable_interrupt();
      expulsion=true;
      activator(scheduler());
      expulsion=false;
      return;
    }else{
      enable_disk_interrupt();
      enable_interrupt();
    }
  
    

    if(running->ticks == 0){
        disable_interrupt();
        disable_disk_interrupt();
        if(!queue_empty(LP)){
          enable_disk_interrupt();
          enable_interrupt();
            activator(scheduler());
        }
        else{
          enable_disk_interrupt();
          enable_interrupt();
          running->ticks=QUANTUM_TICKS;
        }
        return;
     }   
    
  }

  else if(running->priority== SYSTEM){
    disable_interrupt();
    disable_disk_interrupt();
    if(!queue_empty(HP) || !queue_empty(LP)){
      enable_disk_interrupt();
      enable_interrupt();
      activator(scheduler());
    }else{
      enable_disk_interrupt();
      enable_interrupt();
    }
    return;
  }

  else{
    printf("ERROR: Priority Unkown\n");
    exit(-1);
  }
} 

/* Activator */
void activator(TCB* next){
 
  TCB * previous = running;

  running = next;
  current = next->tid;


 
   
  if (previous-> state == FREE){

    printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", previous->tid, current);
    setcontext(&(next->run_env));
   

  }else {

    if(previous->priority == LOW_PRIORITY && (!disk_expulsion || expulsion)){
      previous->ticks = QUANTUM_TICKS;
      disable_interrupt();
      disable_disk_interrupt();
      enqueue(LP,previous);
      enable_disk_interrupt();
      enable_interrupt();
    }
    
    
    
    if(expulsion){
      printf("*** THREAD %i PREEMTED : SETCONTEXT OF %i \n",  previous->tid, current);
    }else if(previous->tid == -1){
      printf("*** THREAD READY : SET CONTEXT TO %i \n",current);
    }
    else{
      printf("*** SWAPCONTEXT FROM %i TO %i\n", previous->tid, current);
    }
    
    swapcontext(&(previous->run_env),&(next->run_env));
  }
  return;
  printf("mythread_free: After setcontext, should never get here!!...\n"); 
}



