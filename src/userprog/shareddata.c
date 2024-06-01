#include "userprog/shareddata.h"
#include <stdlib.h>
#include <stdio.h>

/* Initialize a wait shared data structure.
    Contains the exit status of a child when its parent is waiting on it. */
void initialize_shared_data(shared_data_t* shared_data, enum SDType sdtype) {
  sema_init(&shared_data->semaphore, 0);
  lock_init(&shared_data->lock);
  shared_data->sdtype = sdtype;
  shared_data->ref_cnt = 2;
  shared_data->data = -1;
  shared_data->cpid = thread_current()->tid;
}

/* Waits for a child. Gets its data and frees the shared data struct. */
int wait_for_data(shared_data_t* shared_data, enum SDType sdtype) {
  // if (shared_data->sdtype != sdtype) {
  //   perror("Mismatched enum types waiting for data");
  // }
  sema_down(&shared_data->semaphore);

  ASSERT(shared_data->sdtype == sdtype);

  if (shared_data == NULL) {
    return -1;
  }
  // sema_up(&shared_data->semaphore);
  int data = shared_data->data;
  lock_acquire(&shared_data->lock);
  int ref_cnt = --shared_data->ref_cnt;
  lock_release(&shared_data->lock);
  if (ref_cnt == 0) {
    //list_remove(shared_data->elem); //may or may not need to be synchronized
    if (shared_data->sdtype == WAIT) {
      list_remove(&(shared_data->elem)); //may or may not need to be synchronized
    }
    free(shared_data);
  }
  return data;
}

/* Child processs saves exit status in shared data structure. */
void* save_data(shared_data_t* shared_data, int data, enum SDType sdtype) {
  // if (shared_data->sdtype != sdtype) {
  //   perror("Mismatched enum types saving data");
  // }
  ASSERT(shared_data->sdtype == sdtype);
  // shared_data->cpid = thread_current()->tid;
  shared_data->data = data;
  sema_up(&shared_data->semaphore);
  lock_acquire(&shared_data->lock);
  int ref_cnt = --shared_data->ref_cnt;
  lock_release(&shared_data->lock);
  if (ref_cnt == 0) {
    free(shared_data);
  }
  return NULL;
}

void init_shared_data_list(shared_data_list_t* sd_list) { list_init(sd_list); }

shared_data_t* find_sd(shared_data_list_t* sd_list, tid_t t) {
  struct list_elem* e;

  for (e = list_begin(sd_list); e != list_end(sd_list); e = list_next(e)) {
    shared_data_t* f = list_entry(e, shared_data_t, elem);
    if (f->cpid == t) {
      return f;
    }
  }
  return NULL;
}

/* Process calls this to handle its list children_wait upon exiting. 
  Decrements ref count by 1 in the shared data struct.
  Removes all shared data structs from the Pintos list and frees the list. */
void update_children_sds(shared_data_list_t* sd_list) {
  struct list_elem* e;
  // for (e = list_begin(sd_list); e != list_end(sd_list); e = list_next(e)) {
  //   shared_data_t* f = list_entry(e, shared_data_t, elem);
  //   save_data(f, f->data, WAIT);
  //   list_pop_front()
  // }
  while (!list_empty(sd_list)) {
    e = list_pop_front(sd_list);
    shared_data_t* f = list_entry(e, shared_data_t, elem);
    lock_acquire(&f->lock);
    int ref_cnt = --f->ref_cnt;
    lock_release(&f->lock);
    if (ref_cnt == 0) {
      free(f);
    }
  }
  free(sd_list);
  return NULL;
}