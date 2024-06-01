#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "userprog/shareddata.h"
#include "userprog/syscall.h"
// #include "threads/thread.h"
// #include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

// typedef struct list file_table_t;

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */

typedef struct list file_mappings_t;

struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. Maps to all page tables. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  shared_data_list_t*
      children_wait; /* List of pointers to children's wait shared data structures. */

  // list_init(&t->pcb->children_wait);
  shared_data_t* process_wait; /* This process's wait shared data structure. */
                               // file_table_t fd_table;

  /* FILE-JOIN */
  file_mappings_t* file_list;
  int fd_current;
  //   char* fn_copy; // added for multi-oom
};

void userprog_init(void);

tid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(int exit_status);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
tid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */