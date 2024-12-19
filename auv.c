#define _GNU_SOURCE

#include "auv.h"
#include "log.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h> /* Definition of AT_* constants */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int auv_umount(const char *path) {
  if (umount2(path, MNT_DETACH | MNT_FORCE) != 0) {
    log_error("Failed to unmount %s: %s", path, strerror(errno));
    return -1;
  } else {
    log_debug("Successfully unmounted %s", path);
    return 0;
  }
}

// Cleanup function to unmount the directories at exit
void auv_cleanup_mounts(const char *overlay_dir) {
  char mount_point[256];

  // Unmount /sys
  snprintf(mount_point, sizeof(mount_point), "%s/sys", overlay_dir);
  if (auv_umount(mount_point) != 0) {
    log_error("Failed to unmount %s: %s", mount_point, strerror(errno));
  } else {
    log_debug("Successfully unmounted %s", mount_point);
  }

  // Unmount /dev
  snprintf(mount_point, sizeof(mount_point), "%s/dev", overlay_dir);
  if (auv_umount(mount_point) != 0) {
    log_error("Failed to unmount %s: %s", mount_point, strerror(errno));
  } else {
    log_debug("Successfully unmounted %s", mount_point);
  }

  // Unmount /proc
  snprintf(mount_point, sizeof(mount_point), "%s/proc", overlay_dir);
  if (auv_umount(mount_point) != 0) {
    log_error("Failed to unmount %s: %s", mount_point, strerror(errno));
  } else {
    log_debug("Successfully unmounted %s", mount_point);
  }

  // Unmount the overlayfs
  if (auv_umount(overlay_dir) != 0) {
    log_error("Failed to unmount overlayfs: %s", strerror(errno));
  } else {
    log_debug("Successfully unmounted %s", overlay_dir);
  }
}

void auv_mount_pseudo(const char *sysroot) {
  char mount_point[1024];

  // Mount /proc
  snprintf(mount_point, sizeof(mount_point), "%s/proc", sysroot);
  if (mount("proc", mount_point, "proc", 0, NULL) != 0) {
    log_error("Failed to mount /proc: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Mount /dev
  snprintf(mount_point, sizeof(mount_point), "%s/dev", sysroot);
  if (mount("devtmpfs", mount_point, "devtmpfs", 0, NULL) != 0) {
    log_error("Failed to mount /dev: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Mount /sys
  snprintf(mount_point, sizeof(mount_point), "%s/sys", sysroot);
  if (mount("sysfs", mount_point, "sysfs", 0, NULL) != 0) {
    log_error("Failed to mount /sys: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

void auv_mount_overlay(const char *overlay_home, const char *lowerdir) {
  // Create the overlayfs mount options
  char overlay_dir[256];
  sprintf(overlay_dir, "%s/auv", overlay_home);

  char upperdir[256];
  char workdir[256];
  char mount_opts[1024];

  snprintf(upperdir, sizeof(upperdir), "%s/upperdir", overlay_home);
  snprintf(workdir, sizeof(workdir), "%s/workdir", overlay_home);
  snprintf(mount_opts, sizeof(mount_opts), "lowerdir=%s,upperdir=%s,workdir=%s", lowerdir, upperdir,
           workdir);

  // Mount the overlayfs
  if (mount("overlay", overlay_dir, "overlay", 0, mount_opts) != 0) {
    log_error("Failed to mount overlay: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  auv_mount_pseudo(overlay_dir);

  log_debug("Overlayfs mounted successfully on %s", overlay_dir);
}

void auv_run(char *argv[], const char *overlay_dir) {
  // We need to fork a child process to run the command,
  // otherwise directly execve will clobber any atexit hooks
  pid_t pid = fork();
  if (pid < 0) {
    log_error("Fork failed: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (pid == 0) { // Child process
    // Change the root directory to the overlayfs mount directory
    if (chroot(overlay_dir) != 0) {
      log_error("Failed to change root to overlayfs mount point: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }

    // Change to the new root directory
    if (chdir("/") != 0) {
      log_error("Failed to change directory to new root: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }

    // Run the command with arguments
    if (execvp(argv[0], argv) == -1) {
      log_error("Command execution failed: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }
  } else { // Parent process
    // Wait for the child process to complete
    int status;
    waitpid(pid, &status, 0);

    // Check if the child process exited successfully
    if (WIFEXITED(status)) {
      log_debug("Child process finished with exit code %d", WEXITSTATUS(status));
    } else {
      log_info("Child process terminated abnormally");
    }
  }
}

int auv_apply(char *base, char *diff, char *target, int ignore_exist) {
  // build an out-of-tree new root using hardlinks or symlinks

  // if target exist:
  //      case 0: skip, maybe already handled
  //  elif diff not exist:
  //      case 1: no change under base, create links
  //      switch base:
  //          case 1.1: base is DIR, create symlink
  //          else 1.2: base is sth. else, create hardlink
  //  else:
  //      case 2: something changed under base
  //      switch diff:
  //          case 2.1: diff is CHR, remove target
  //          case 2.2: diff is DIR, recursively apply
  //          else 2.3: diff is sth. else, create hardlinks

  if (auv_exist(target)) {
    log_debug("skip %s %s", target, "success");
  } else {
    log_debug("apply %s %s", target, "success");
  }

  if (auv_exist(target)) {
    // case 0
    return 0;
  }

  if (!auv_exist(diff)) {
    // case 1

    struct stat base_st;
    if (auv_lstat(base, &base_st) != 0) {
      log_error("Failed to stat %s: %s", base, strerror(errno));
      return -1;
    }

    if (auv_isdir(&base_st)) {
      // case 1.1: base is dir -- must symlink

      // yes, we need to link `target` rather than `base`
      // since afterwards they will be exchanged
      int err = symlink(target, target);
      log_trace("symlink %s %s %s", base, target, err == 0 ? "success" : "failed");
      if (err != 0)
        log_error("1 Failed to symlink %s: %s", base, strerror(errno));
      return 0;

    } else {
      // case 1.2: everything else -- do hardlink
      int err = link(base, target);
      log_trace("link %s %s %s", base, target, err == 0 ? "success" : "failed");
      if (err != 0)
        log_error("1 Failed to link %s: %s", base, strerror(errno));
      return 0;
    }

  } else {
    // case 2: something changed under base

    struct stat diff_st;
    auv_lstat(diff, &diff_st);

    if (auv_ischar(&diff_st)) {
      // case 2.1
      // diff is character special file
      // whic means diff is removed from the base
      // do nothing, i.e. this target will not be present
      log_debug("remove %s %s %s", diff, target, "success");

    } else if (auv_isdir(&diff_st)) {
      // case 2.2: diff is DIR, recursively apply

      // create the target directory
      auv_ensure_path_is_dir(target);

      auv_apply_dir(base, diff, target, diff, ignore_exist);
      auv_apply_dir(base, diff, target, base, true);

    } else {
      // else 2.3: diff is sth. else, create hardlinks
      int err = link(diff, target);
      int myerrno = errno;
      log_debug("link %s %s %s", diff, target, err == 0 ? "success" : "failed");
      if (err != 0) {
        log_error("Failed to link %s to %s: %s", diff, target, strerror(myerrno));
        exit(EXIT_FAILURE);
      }
      return 0;
    }
  }
  return 0;
}

int auv_apply_dir(char *base, char *diff, char *target, char *_dir, int ignore_exist) {
  DIR *dir = opendir(_dir);

  log_debug("open %s %s", _dir, dir ? "success" : "failed");
  if (!dir) {
    log_error("Failed to open diff directory: %s", strerror(errno));
    return -1;
  }

  // iterate over all entries in the diff directory
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    // build the full path for the child
    char base_child[1024], diff_child[1024], target_child[1024];
    snprintf(base_child, sizeof(base_child), "%s/%s", base, entry->d_name);
    snprintf(diff_child, sizeof(diff_child), "%s/%s", diff, entry->d_name);
    snprintf(target_child, sizeof(target_child), "%s/%s", target, entry->d_name);

    // recursively apply diff to the child
    int err = auv_apply(base_child, diff_child, target_child, ignore_exist);
    if (err != 0) {
      log_debug("Failed to process %s", entry->d_name);
      closedir(dir);
      return -1;
    }
  }

  closedir(dir);
  return 0;
}

int auv_commit(char *base, char *target) {
  log_trace("commit %s %s", base, target);
  return auv_exchange(base, target);
}

int auv_finialize(char *base, char *target) {
  // traverse all files under base, when encounter a symlink to target, swap them

  // if base is symlink
  //     if base -> target:
  //         case 1.1 exchange them, this also ends DFS
  //     else
  //        case 1.2 ignore, as they are created by us
  //  elif base is dir
  //      case 2: recursively call finalize on children
  //  else:
  //      case 3: ignore, as they are already copied or linked by us

  log_trace("finalize %s %s", base, target);

  struct stat base_st;
  if (lstat(base, &base_st) != 0) {
    log_error("Failed to stat %s: %s", base, strerror(errno));
    return -1;
  }

  if (auv_issymlink(&base_st)) {
    // case 1

    char real_base[1024];
    ssize_t len = readlink(base, real_base, sizeof(real_base));

    if ((size_t)len == strlen(target) && strncmp(real_base, target, len) == 0) {
      // case 1.1: base -> target, exchange them
      return auv_exchange(base, target);
    } else {
      // case 1.2 ignore
      log_debug("ignoring symlink at %s to %s", base, real_base);
      return 0;
    }

  } else if (auv_isdir(&base_st)) {
    // case 2: recursively call finalize on children
    return auv_finialize_dir(base, target);

  } else {
    // case 3: ignore, as they are already copied or linked by us
    return 0;
  }
}

int auv_finialize_dir(char *base, char *target) {
  // recursively call finalize on children
  DIR *dir = opendir(base);
  if (!dir) {
    log_error("Failed to open base directory: %s", strerror(errno));
    return -1;

  } else {
    // iterate over all entries in the base directory

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      // build the full path for the child
      char base_child[1024], target_child[1024];
      snprintf(base_child, sizeof(base_child), "%s/%s", base, entry->d_name);
      snprintf(target_child, sizeof(target_child), "%s/%s", target, entry->d_name);

      // recursively apply diff to the child
      int err = auv_finialize(base_child, target_child);
      if (err != 0) {
        log_error("Failed to process %s", entry->d_name);
        closedir(dir);
        return -1;
      }
    }

    closedir(dir);
  }
  return 0;
}

/* ----------------------------------------------------------------
 * Helper functions
 * ---------------------------------------------------------------- */

int auv_exist(const char *path) {
  struct stat st;
  return lstat(path, &st) == 0;
}

int auv_lstat(const char *path, struct stat *st) {
  if (lstat(path, st) != 0) {
    log_error("Failed to stat %s: %s", path, strerror(errno));
    return -1;
  }
  return 0;
}

int auv_ischar(struct stat *st) { return S_ISCHR(st->st_mode); }
int auv_isdir(struct stat *st) { return S_ISDIR(st->st_mode); }
int auv_issymlink(struct stat *st) { return S_ISLNK(st->st_mode); }

int auv_exchange(char *base, char *target) {
  log_trace("exchange %s %s", base, target);
  int err = renameat2(AT_FDCWD, base, AT_FDCWD, target, RENAME_EXCHANGE);
  if (err != 0) {
    log_error("exchaneg %s %s failed: %s", base, target, strerror(errno));
    return -1;
  }
  return 0;
}

int auv_mkdir(const char *path) {
  if (mkdir(path, 0755) != 0) {
    log_error("mkdir %s failed: %s", path, strerror(errno));
    return -1;
  }
  return 0;
}

int auv_ensure_path_is_dir(const char *path) {
  if (!auv_exist(path)) {
    return auv_mkdir(path);
  } else {
    struct stat st;
    if (auv_lstat(path, &st) != 0) {
      log_error("Failed to stat %s: %s", path, strerror(errno));
      return -1;
    }
    if (!auv_isdir(&st)) {
      log_error("Path exists but is not a directory: %s", path);
      return -1;
    }
  }
  return 0;
}
