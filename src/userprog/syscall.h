#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

// Lock to prevent multiple syscalls simultaneously

#include "lib/kernel/list.h"
#include <stdbool.h>

// struct lock lock;

// New file struct to map a file to it's file descriptor and name
typedef struct file_map {
  int fd;
  // const char* name;
  // bool is_open;
  int size;
  struct file* file;
  struct list_elem elem;
} file_map_t;

typedef struct list file_table_t;

// List of files for a given process
// typedef struct file_table {
//   struct list lst;
// }; file_table_t;

#include "userprog/process.h"

//#define SYS_PRACTICE 333
void syscall_init(void);
int practice(int i);
void halt(void);
void exit(int status);
bool validate_ptr(void* ptr);
bool validate_string(const char* str);
int wait(tid_t pid);
tid_t exec(const char* cmd_line);
int update_process(struct file* f);
void valid_ptr(void* ptr, size_t i);
struct file_map* get_file(int fd);

/* global lock for file sys */
//static struct lock fs_lock;

/* File operations helper functions */
// void s_create(void* str, int size, struct intr_frame* f);
// void s_remove(void* str, struct intr_frame* f);
// void s_open(void* str, struct intr_frame* f);
// void s_filesize(int fd, struct intr_frame* f);
// void s_read(int fd_, void* buffer, int size, struct intr_frame* f);
// void s_write(int fd_, void* buffer, int size, struct intr_frame* f);
// struct file* getFile(int fd);

#endif /* userprog/syscall.h */