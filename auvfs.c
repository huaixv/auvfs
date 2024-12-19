#include "auv.h"
#include "log.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

const char *overlay_home = "/mnt/";
const char *overlay_dir = "/mnt/auv";

const char *lowerdir = "/";
const char *upperdir = "/mnt/upperdir";
const char *workdir = "/mnt/workdir";

const char *targetdir = "/mnt/newroot";

void cleanup_mounts() { auv_cleanup_mounts(overlay_dir); }

void usage(int __unused argc, char **argv) {
  log_fatal("Usage: %s [--all|--apply|--commit|--finalize]"
            " [--loglevel trace|debug|info|warn|error|fatal]"
            " command [args...]",
            argv[0]);
}

int do_all = 0;
int do_apply = 0;
int do_commit = 0;
int do_finalize = 0;

int log_level = LOG_INFO;

int parse_args(int argc, char **argv) {
  if (argc < 2) {
    usage(argc, argv);
    exit(EXIT_FAILURE);
  }

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--all") == 0) {
      do_all = 1;
      do_apply = do_commit = do_finalize = 1;
    } else if (strcmp(argv[i], "--apply") == 0) {
      do_apply = 1;
    } else if (strcmp(argv[i], "--commit") == 0) {
      do_commit = 1;
    } else if (strcmp(argv[i], "--finalize") == 0) {
      do_finalize = 1;
    } else if (strcmp(argv[i], "--loglevel") == 0) {
      i++;
      if (strcmp(argv[i], "trace") == 0) {
        log_level = LOG_TRACE;
      } else if (strcmp(argv[i], "debug") == 0) {
        log_level = LOG_DEBUG;
      } else if (strcmp(argv[i], "info") == 0) {
        log_level = LOG_INFO;
      } else if (strcmp(argv[i], "warn") == 0) {
        log_level = LOG_WARN;
      } else if (strcmp(argv[i], "error") == 0) {
        log_level = LOG_ERROR;
      } else if (strcmp(argv[i], "fatal") == 0) {
        log_level = LOG_FATAL;
      } else {
        log_warn("Invalid log level: %s", argv[i]);
      }
    } else {
      break;
    }
  }

  if (!do_all && !do_apply && !do_commit && !do_finalize) {
    do_all = 1;
    do_apply = do_commit = do_finalize = 1;
  }

  log_set_level(log_level);

  return i;
}

void dump_args(char **argv) {
  log_debug("do_all: %d, do_apply: %d, do_commit: %d, do_finalize: %d", do_all, do_apply, do_commit,
            do_finalize);

  for (int i = 0; argv[i] != NULL; ++i) {
    log_debug("argv[%d]: %s", i, argv[i]);
  }
}

int main(int argc, char **argv) {
  int num_opts = parse_args(argc, argv);
  argc -= num_opts;
  argv += num_opts;

  dump_args(argv);

  atexit(cleanup_mounts);

  // Ensure required directories exist
  if (auv_ensure_path_is_dir(overlay_dir) != 0 || auv_ensure_path_is_dir(lowerdir) != 0 ||
      auv_ensure_path_is_dir(upperdir) != 0 || auv_ensure_path_is_dir(workdir) != 0) {
    return EXIT_FAILURE;
  }

  // try mount
  auv_mount_overlay(overlay_home, lowerdir);

  // Run the command provided via command-line arguments
  auv_run(argv, overlay_dir);

  // working on "/" is a bit tricky
  // in most cases atomic update to these dirs is sufficient
#define MAP_ROOT(f) f("/usr") f("/etc") f("/var") f("/opt") f("/boot")

  auv_mkdir("/mnt/newroot");

#define AUV_APPLY_DIFF(root) auv_apply(root, "/mnt/upperdir" root, "/mnt/newroot" root, 0);
  MAP_ROOT(AUV_APPLY_DIFF);

#define AUV_COMMIT_DIFF(root) auv_commit(root, "/mnt/newroot" root);
  MAP_ROOT(AUV_COMMIT_DIFF);

#define AUV_FINALIZE(root) auv_finialize(root, "/mnt/newroot" root);
  MAP_ROOT(AUV_FINALIZE);

  return EXIT_SUCCESS;
}
