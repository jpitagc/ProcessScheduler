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

TCB* scheduler();  
void activator(); 
void timer_interrupt(int sig);  
void disk_interrupt(int sig);   


static TCB t_state[N];  


static TCB* running;
static int current = 0;


static int init=0;
// Indica si se ha producido una expulsion de un proceso High Priority a uno Low Priority.
static bool expulsion=false;

// Creamos ambas colas. HP--> High Priority , LP--> Low Priority 
struct queue * HP;
struct queue * LP;

static TCB idle;
static void idle_function(){
  while(1);
}


void init_mythreadlib() {
  
  // Inicializamos ambas colas.
  LP= queue_new();
  HP= queue_new();
  int i;  

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

 
  init_disk_interrupt();
  init_interrupt();
}



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

  

// En funcion del tipo de priotidad del proceso creado le asignamos unos ciclos de reloj u otros (-1 en caso de High Priority) 
// Ademas de encolarlo en la cola que le corresponda.
if(priority == HIGH_PRIORITY){
  t_state[i].ticks = -1;
  disable_interrupt();
  enqueue(HP, &t_state[i]);
  enable_interrupt();

}else if (priority == LOW_PRIORITY){
  t_state[i].ticks = QUANTUM_TICKS;
  disable_interrupt();
  enqueue(LP, &t_state[i]);
  enable_interrupt();

}else{
  printf("ERROR: Priority of thead neither high neither low\n");
  exit(-1);
}
  printf("Thread %i created priority %i \n",i,priority);
  return i;




} 







int read_disk()
{
   return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{
} 



void mythread_exit() {
  int tid = mythread_gettid();	

  printf("*** THREAD %d FINISHED\n", tid);	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 

  TCB* next = scheduler();
  activator(next);
}


void mythread_setpriority(int priority) {
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
}


int mythread_getpriority(int priority) {
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}

// El siguiente proceso a ejecutar sera un proceso en este orden:
// 1. En el caso de que la cola de High Priority no este vacia, el primero de dicha cola.
// 2. En el caso de que la cola de Low Priority no este vacia, el primero de dicha cola.
// 3. Si no se cumplen ni caso 1 ni caso 2 acabamos el programa debido a que no hay mas procesos para ejecutar.
TCB* scheduler(){

  /*
  printf("\nCola De Alta Prioridad ");
  queue_print(HP);
  printf("\nCola De Baja Prioridad ");
  queue_print(LP);
  printf("\n");*/

  TCB* proceso;
  disable_interrupt();
  if(!queue_empty(HP)){
    proceso= dequeue(HP);
  }
  else if (!queue_empty(LP)){
    proceso = dequeue(LP);
  }
  else{
    printf("*** FINISH \n");
    enable_interrupt();
    exit(1);
  }
  enable_interrupt();
  return proceso;


  printf("mythread_free: No thread in the system\nExiting...\n");	
  exit(1); 
}


// Time interrupt funcionara de manera distinta dependiendo de el tipo de prioridad del proceso ejecutando.
// High Priority --> Comprobara si a acabado la ejecucion. Si no ha acabado dejara que siga corriendo, en caso contrario llamara al activador.
// Low  Priority --> Primero se comprueba si el proceso ha cabado su ejecucion, si es asi se llamara al activador.
//                   Segundo se comprueba si la lista de procesos High Priority esta vacia, sino es asi se llamara al activador.
//                   Finalmente si los ciclos de reloj restantes de este proceso han llegado a su fin y la lista de procesos Low Priority 
//                   no esta vacia se llamara al activador para que realice el cambio de proceso.
//                   En cualquier caso reducira el numero de ciclos de reloj restantes en 1.  
void timer_interrupt(int sig)
{
  
  

  if(running->priority==HIGH_PRIORITY){
    if(running->state != FREE){
      return;
    }else{
      activator(scheduler());
    }
    
  }
  else if (running->priority == LOW_PRIORITY){

    running->ticks--;

    if(running->state == FREE){
        activator(scheduler());
    }
    
    disable_interrupt();
    if(!queue_empty(HP)){
      running->ticks = QUANTUM_TICKS;
      enqueue(LP,running);
      enable_interrupt();
      expulsion=true;
      activator(scheduler());
      
      return;
    }else {enable_interrupt();}
    
    

    if(running->ticks == 0){
       running->ticks=QUANTUM_TICKS;
        disable_interrupt();
        if(!queue_empty(LP)){
           enqueue(LP,running);
            enable_interrupt();
            activator(scheduler());
        }
        else{
          enable_interrupt();
          
        }
         
     }   
    
    
    

  }


  else{
    printf("ERROR: Priority Unkown\n");
    exit(-1);
  }

    
} 

// Misma funcionalidad que en el ejecicio RR excepto de que si se produce una expulsion debido a la llegada de un proceso High Priority
// el mensaje que sale por terminal varia.
void activator(TCB* next){

  TCB * previous = running;

  running = next;
  current = next->tid;


 
   
  if (previous-> state == FREE){

    printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", previous->tid, current);
    setcontext(&(next->run_env));
   

  }else {
 
    if(expulsion){
      printf("*** THREAD %i PREEMTED : SETCONTEXT OF %i \n",  previous->tid, current);
      expulsion=false;
    }else{
      printf("*** SWAPCONTEXT FROM %i TO %i\n", previous->tid, current);
    }
    
    swapcontext(&(previous->run_env),&(next->run_env));
  }
  return;
  printf("mythread_free: After setcontext, should never get here!!...\n");  
}



