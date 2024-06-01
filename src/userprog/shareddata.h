#ifndef USERPROG_SHAREDDATA_H
#define USERPROG_SHAREDDATA_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <stdint.h>

enum SDType { WAIT, LOAD };

/* Shared data structure. */
typedef struct shared_data {
  tid_t cpid;
  struct list_elem elem;
  struct semaphore semaphore;
  struct lock lock;
  int ref_cnt;
  enum SDType sdtype;
  int data; /* if WAIT: exit status of child. if LOAD: load success of child. */
} shared_data_t;

// struct list shared_data_list;
typedef struct list shared_data_list_t;

/* Shared data structure for child's load status. */
// typedef struct shared_data_load {
//   struct list_elem elem;
//   struct semaphore sema_load;
//   struct lock lock;
//   int ref_cnt;
//   int load_success; /* whether child was successfully loaded */
// } shared_data_load_t;

// typedef shared_data_load_t* shared_data_load_list_t;

void initialize_shared_data(shared_data_t* shared_data, enum SDType sdtype);
int wait_for_data(shared_data_t* shared_data, enum SDType sdtype);
void* save_data(shared_data_t* shared_data, int data, enum SDType sdtype);
shared_data_t* find_sd(shared_data_list_t* sd_list, tid_t t);
void update_children_sds(shared_data_list_t* sd_list);
void init_shared_data_list(shared_data_list_t* sd_list);

// void initialize_shared_data_load(shared_data_load_t* shared_data);
// int wait_for_data_load(shared_data_load_t* shared_data);
// void* save_data_load(void* shared_pg, int load_success);

#endif /* userprog/shareddata.h */