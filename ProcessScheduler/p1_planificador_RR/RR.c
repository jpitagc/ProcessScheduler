#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

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

// Creacion de unica cola (NP--> No Priority)
struct queue * NP;


static TCB idle;
static void idle_function(){
  while(1);
}


void init_mythreadlib() {

  // Inicializar a cola
  NP= queue_new(); //no priority

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

  // Añadimos el numero de ciclos de reloj maximo
  t_state[i].ticks = QUANTUM_TICKS;

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


  // Encolamos el proceso que se acaba de crear en NP
  disable_interrupt(); // En acciones que modifican la cola, por seguridad, se bloquean las interrupciones
  enqueue(NP, &t_state[i]);
  enable_interrupt(); // Y después se reactivan
  return i;
}

int read_disk()
{
   return 1;
}


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



TCB* scheduler(){

  // Proximo proceso a ejecutar es el primero de la cola NP siempre que esta no este vacia
  // en caso de que la cola este vacia no hay mas procesos a ejecutar por lo que finaliza el programa.
  disable_interrupt();
  if(!queue_empty(NP)){

    TCB* p = dequeue(NP);
    enable_interrupt();
    return p;
  }
  else {
    printf("*** FINISH \n");
    enable_interrupt();
    exit(1);
  }


  printf("mythread_free: No thread in the system\nExiting...\n");
  exit(-1);
}



void timer_interrupt(int sig)
{
  // Reducimos el numero de ciclos de reloj que quedan para el proceso en ejecución.
  running->ticks--;

  // Si los ciclos de ejecucuion restantes llegan acero ese proceso es expulsado por el siguiente en la cola
  if(running->ticks == 0 || running->state == FREE){

    activator(scheduler());

  }
}


void activator(TCB* next){
  // Almacenamos temporalmente el proceso que este corriendo y actualizamos al que va a correr ahora
  TCB * previous = running;
  running = next;
  current = next->tid;

  // En caso de que el thread expulsado haya terminado la ejecucion realizamos un setcontext (sin almacenar el context anterior)
  if (previous-> state == FREE){

    printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", previous->tid, current);
    setcontext(&(next->run_env));


  }else { // En caso de que no haya acabado su ejecucion le reestablecemos los ciclos de reloj y lo encolamos en la cola NP.
          // ademas realizamos un swapcontext para no perder el contexto de el proceso que ha sido expulsado.
    previous->ticks = QUANTUM_TICKS;

    disable_interrupt();
    enqueue(NP,previous);
    enable_interrupt();

    printf("*** SWAPCONTEXT FROM %i TO %i\n", previous->tid, current);
    swapcontext(&(previous->run_env),&(next->run_env));
  }
  return;
  printf("mythread_free: After setcontext, should never get here!!...\n");

}
