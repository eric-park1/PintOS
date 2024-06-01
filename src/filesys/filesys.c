#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);
static int get_next_part(char part[NAME_MAX + 1], const char** srcp);
bool* parse_path(const char* path, char fn[NAME_MAX + 1], struct dir** dir);
struct inode* cwd();
static char* dn(char* path);
static char* dpath(char* path);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();
  cache_init();

  if (format)
    do_format();

  thread_current()->cwd = dir_open(inode_open(ROOT_DIR_SECTOR));
  free_map_open();
}

/* Gets the inode of the current working directory. */
struct inode* cwd() {
  dir_inode(thread_current()->cwd);
}

/* Reopens the inode of the current working directory. */
struct dir* reopen_cwd() {
  dir_reopen(thread_current()->cwd);
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part.
   Function is given in project 3 spec. */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  char* dst = part;

  /* Skip leading slashes. If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX chars from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Parse a given path PATH to see if directory exists.
  If the last part of the PATH is a file, put the name in fn.
  If the last part of the PATH is a directory, stores the directory in DIR. */
bool* parse_path(const char* path, char fn[NAME_MAX + 1], struct dir** dir) {
  if (strcmp(path, "\0") == 0)
    return false;

  char* d = dn(path);
  struct inode* curr;
  struct inode* next;
  int ind = 0;
  struct dir* directory;

  /* Retrieves the first directory of PATH. */
  if (path[0] == '/') {
    curr = dir_inode(dir_open_root());
  } else if (thread_current()->cwd != NULL) {
    curr = dir_inode(dir_reopen(thread_current()->cwd));
  } else {
    curr = NULL;
  }

  if (curr == NULL) {
    return false;
  }

  if (strcmp(d, ".") == 0) {
    strlcpy(fn, path, sizeof(char) * (strlen(path) + 1));
  } else {
    /* Iterate through directories to make sure that 
    NEXT is a part of the curr path's (CURR) directory 
    and is valid. */
    while (get_next_part(fn, &d) > 0 && curr != NULL) {
      directory = dir_open(curr);
      if (dir_lookup(directory, fn, &next)) {
        dir_close(directory);
        if (next != NULL && inode_directory(next)) {
          curr = next;
        }
      } else if (get_next_part(fn, &d) != 0) {
        return false;
      } else {
        break;
      }
      ind++;
    }
  }

  char* n = dpath(path);
  strlcpy(fn, n, sizeof(char) * (strlen(n) + 1));
  free(n);

  if ((*dir = dir_open(curr)) == NULL) {
    return false;
  } else {
    return true;
  }
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  cache_flush();
  free_map_close();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size, bool is_dir) {
  // Verifies that the length of the file is valid
  if (strlen(name) > NAME_MAX) {
    return false;
  }
  block_sector_t inode_sector = 0;
  char fn[NAME_MAX + 1];
  struct dir* dir = NULL;
  struct inode* inode = NULL;

  /* Gets the inode for the directory. */
  if (name[0] == '/') {
    inode = dir_inode(dir_open_root());
  } else if (thread_current()->cwd != NULL) {
    inode = dir_inode(dir_reopen(thread_current()->cwd));
    if (inode_removed(cwd())) { // Cannot create a file in a removed cwd
      return false;
    }
  } else {
    inode = NULL;
    if (inode_removed(cwd())) { // Cannot create a file in a removed cwd
      return false;
    }
  }

  /* Creating the file. */
  bool success =
      (parse_path(name, fn, &dir) && free_map_allocate(1, &inode_sector) &&
       inode_create(inode_sector, initial_size, is_dir) && dir_add(dir, fn, inode_sector));

  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);

  dir_close(dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise. Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct file* f;
  if (strcmp(name, "") == 0) {
    return NULL;
  }

  bool success;
  char fn[NAME_MAX + 1];
  struct dir* dir = dir_open_root();
  struct inode* inode = NULL;

  if (strcmp(name, "/") == 0) {
    f = file_open(dir_inode(dir));
    dir_close(dir);
    return f;
  } else if (strcmp(name, ".") == 0) {
    if (inode_removed(cwd())) {
      return NULL;
    }
    struct file* cwd = file_open(dir_inode(dir_reopen(thread_current()->cwd)));
    dir_close(dir);
    return cwd;
  }

  char* d_name = dpath(name);
  if (dir == NULL || !dir_lookup(dir, d_name, &inode)) {
    success = parse_path(name, fn, &dir);
    if (success) {
      dir_lookup(dir, fn, &inode);
    } else {
      return NULL;
    }
  }

  dir_close(dir);
  free(d_name);
  return file_open(inode);
}

/* Changes the current process's directory 
  to the new directory given by PATH. 
  Returns TRUE if successful and FALSE otherwise. */
bool filesys_chdir(const char* path) {
  struct dir* dir;
  if (strcmp(path, "..") == 0) { // Directory before
    thread_current()->cwd = dir_open(inode_open(inode_parent(cwd())));

    dir_close(dir_reopen(thread_current()->cwd));

    return true;
  }

  char fn[NAME_MAX + 1];
  parse_path(path, fn, &dir);
  struct inode* dir_inode = NULL;

  if (dir_lookup(dir, fn, &dir_inode)) {
    dir_close(thread_current()->cwd);
    thread_current()->cwd = dir_open(dir_inode);
    return true;
  }
  return false;
}

/* Helper for parse_path.
  Returns the given PATH with removed unncessary characters. */
static char* dpath(char* path) {
  char* cpy = malloc(strlen(path) + 1);
  strlcpy(cpy, path, sizeof(char) * (strlen(path) + 1));
  if (cpy == NULL || cpy[0] == '\0')
    return "";

  int ind = strlen(cpy) - 1;
  while (ind >= 0 && cpy[ind] == '/') {
    ind--;
  }

  if (ind == -1) {
    return "/";
  }

  cpy[ind + 1] = '\0';
  while (ind >= 0 && cpy[ind] != '/') {
    ind--;
  }
  int len = strlen(&cpy[ind + 1]);
  char* name = malloc(len + 1);
  strlcpy(name, &cpy[ind + 1], sizeof(char) * (len + 1));
  free(cpy);
  return name;
}

/* Helper for parse_path.
  Returns the first directory in PATH 
  with removed unnecessary characters. */
static char* dn(char* path) {
  char* cpy = malloc(strlen(path) + 1);
  strlcpy(cpy, path, sizeof(char) * (strlen(path) + 1));

  if (cpy == NULL || cpy[0] == '\0') {
    return "/";
  }

  int ind = strlen(cpy) - 1;
  // Finds index of first non-/ from the end of the path
  while (ind >= 0 && cpy[ind] == '/') {
    ind--;
  }

  if (ind == -1) {
    return cpy;
  }

  // Finds index of the next / character
  ind--;
  while (ind >= 0 && cpy[ind] != '/') {
    ind--;
  }

  if (ind == -1) {
    return ".";
  }

  cpy[ind] = '\0'; // Null terminate at this /
  ind--;
  // Finds index of next non-/ from current i moving down
  while (ind >= 0 && cpy[ind] == '/') {
    ind--;
  }

  if (ind == -1) {
    return "/";
  }

  ind++;
  cpy[ind] = '\0';
  return cpy;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) {
  if (strcmp(name, "/") == 0) { // Cannot remove root
    return false;
  }

  bool success;
  char fn[NAME_MAX + 1];
  struct dir* dir = NULL;
  success = parse_path(name, fn, &dir);
  struct inode* inode = NULL;
  dir_lookup(dir, fn, &inode);
  // No removing parent or cwd
  if (inode_directory(inode) && !(inode_get_inumber(inode) == inode_get_inumber(cwd()))) {
    if (inode == NULL || cwd() == NULL ||
        inode_get_inumber(inode_open(inode_parent(cwd()))) == inode_get_inumber(inode)) {
      return false;
    }
  }

  success = success && dir_remove(dir, fn);
  dir_close(dir);
  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}