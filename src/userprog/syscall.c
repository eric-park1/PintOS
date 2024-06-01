#include "filesys/filesys.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "devices/block.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include <string.h>

struct lock flock;
static void syscall_handler(struct intr_frame*);

/* Initializes the syscall handler. */
void syscall_init(void) {
  lock_init(&flock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Adds a file to a process's list of all files. */
int update_process(struct file* f) {
  struct thread* curr_thread = thread_current();
  struct file_map* filemap = malloc(sizeof(struct file_map));
  filemap->file = f;
  filemap->fd = curr_thread->pcb->fd_current++;
  list_push_back(curr_thread->pcb->file_list, &filemap->elem);
  return filemap->fd;
}

/* Validates that the pointer PTR is in user memory and is mapped to a kernel address. */
bool vaddress(void* ptr) {
  if (!is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pcb->pagedir, ptr)) {
    return false;
  }
  return true;
}

/* Validates that a string STR is fully in userspace. */
bool vstring(const char* str) {
  for (int i = 0; i <= strlen(str); i++) {
    if (!vaddress(str[i])) {
      return false;
    }
  }
}

/* Verifies that a ptr PTR and the contents of that ptr, of size i, are fully in userspace. */
void valid_ptr(void* ptr, size_t i) {
  if (!vaddress(ptr) || !vaddress(ptr + i)) {
    exit(-1);
  }
}

/* Retrieves a file given a file descriptor FD. */
struct file_map* get_file(int fd) {
  struct file_mappings* file_tbl = thread_current()->pcb->file_list;
  struct list_elem* iter;
  struct file_map* temp_file;
  for (iter = list_begin(file_tbl); iter != list_end(file_tbl); iter = list_next(iter)) {
    temp_file = list_entry(iter, struct file_map, elem);
    if (temp_file->fd == fd) {
      return temp_file; // Return a pointer to the file.
    }
  }
  return NULL; // Return NULL if the file with the given fd is not found.
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);
  valid_ptr((void*)args, sizeof(uint32_t));

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */
  switch (args[0]) {
    case SYS_READ:
      valid_ptr((void*)args[2], args[3]);
      break;
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_WRITE:
      valid_ptr(&args[3], sizeof(uint32_t));
      break;
    case SYS_EXIT:
      f->eax = args[1];
      exit(args[1]);
      break;
    case SYS_EXEC:
      valid_ptr((void*)&args[1], sizeof(uint32_t));
      valid_ptr((void*)args[1], sizeof(uint32_t));
      lock_acquire(&flock);
      f->eax = (pid_t)process_execute((char*)args[1]);
      lock_release(&flock);
      break;
    case SYS_WAIT:
      f->eax = (tid_t)wait((tid_t)args[1]);
      break;
    case SYS_COMPUTE_E:
      f->eax = sys_sum_to_e(args[1]);
      break;
    /* Project 2 Synchronization syscalls */
    case SYS_LOCK_INIT:
      lock_init(args[1]);
      break;
    case SYS_LOCK_ACQUIRE:
      lock_acquire(args[1]);
      break;
    case SYS_LOCK_RELEASE:
      lock_release(args[1]);
      break;
    case SYS_SEMA_INIT:
      sema_init(args[1], (int)args[2]);
      break;
    case SYS_SEMA_DOWN:
      sema_down(args[1]);
      break;
    case SYS_SEMA_UP:
      sema_up(args[1]);
      break;
    case SYS_GET_TID:
      f->eax = thread_current()->tid;
      break;
    /* Project 3: buffer cache custom tests */
    case SYS_CACHE_RESET:
      cache_reset();
      break;
    case SYS_GET_CACHE_HIT:
      f->eax = get_cache_hit();
      break;
    case SYS_GET_CACHE_MISS:
      f->eax = get_cache_miss();
      break;
    case SYS_BLOCKS_READ:
      f->eax = get_read_cnt(fs_device);
      break;
    case SYS_BLOCKS_WRITE:
      f->eax = get_write_cnt(fs_device);
      break;
  }

  if (args[0] == SYS_READ && args[1] == 0) {
    // Read from stdin.
    uint8_t* buffer = (uint8_t*)args[2];
    size_t i = 0;
    while (i < args[3]) {
      buffer[i] = input_getc();
      if (buffer[i++] == '\n')
        break;
    }
    f->eax = i;
  }

  else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  }

  else if (args[0] == SYS_WRITE && args[1] == 1) {
    putbuf((void*)args[2], args[3]);
    f->eax = args[3];
  }

  else if (args[0] == SYS_CREATE) {
    valid_ptr((void*)args[1], 0);
    if (!args[1] || !is_user_vaddr((const void*)args[1])) {
      exit(-1);
    }
    f->eax = filesys_create((char*)args[1], args[2], false);
  }

  else if (args[0] == SYS_REMOVE) {
    f->eax = filesys_remove((char*)args[1]);
  }

  else if (args[0] == SYS_OPEN) {
    if ((void*)args[1] == NULL || !vaddress((void*)args[1])) {
      exit(-1);
    }
    struct file* opened_file = filesys_open((char*)args[1]);
    if (opened_file) {
      f->eax = update_process(opened_file);
    } else {
      f->eax = -1;
    }
  }

  else if (args[0] == SYS_MKDIR) {
    f->eax = filesys_create((char*)args[1], 0, true);
  } else if (args[0] == SYS_CHDIR) {
    f->eax = filesys_chdir((char*)args[1]);
  }

  else if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    exit(args[1]);
  }

  else {
    struct file_map* filemap = get_file(args[1]);
    if (filemap == NULL) {
    } else if (args[0] == SYS_FILESIZE) {
      f->eax = file_length(filemap->file);
    } else if (args[0] == SYS_READ) {
      valid_ptr((void*)args[2], args[3]);
      if (file_directory(filemap->file)) { // if it's a directory, don't allow R/W to it
        f->eax = -1;
      } else {
        lock_acquire(&flock);
        f->eax = file_read(filemap->file, (void*)args[2], args[3]);
        lock_release(&flock);
      }
    } else if (args[0] == SYS_WRITE) {
      valid_ptr((void*)args[2], args[3]);
      if (file_directory(filemap->file)) {
        f->eax = -1;
      } else {
        lock_acquire(&flock);
        f->eax = file_write(filemap->file, (void*)args[2], args[3]);
        lock_release(&flock);
      }
    } else if (args[0] == SYS_SEEK) {
      file_seek(filemap->file, args[2]);
    } else if (args[0] == SYS_TELL) {
      f->eax = file_tell(filemap->file);
    } else if (args[0] == SYS_CLOSE) {
      file_close(filemap->file);
      list_remove(&filemap->elem);
      free(filemap);
    } else if (args[0] == SYS_ISDIR) {
      f->eax = file_directory(filemap->file);
    } else if (args[0] == SYS_INUMBER) {
      f->eax = get_inumber(filemap->file);
    } else if (args[0] == SYS_READDIR) {
      f->eax = dir_readdir((struct dir*)filemap->file, (char*)args[2]);
    }
  }
}

void halt(void) { shutdown_power_off(); }

void exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit(status);
}

int wait(pid_t childpid) {
  if (find_sd(thread_current()->pcb->children_wait, childpid)) {
    return (int)process_wait(childpid);
  }
  return -1;
}

pid_t exec(const char* cmd_line) {
  valid_ptr(cmd_line, sizeof(uint32_t));
  return (pid_t)process_execute((char*)cmd_line);
}
