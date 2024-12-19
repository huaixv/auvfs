#pragma once

#include <sys/stat.h>
#include <sys/types.h>

#define __unused __attribute__((unused))

void auv_cleanup_mounts(const char *overlay_dir);

void auv_mount_overlay(const char *overlay_dir, const char *lowerdir);
void auv_mount_pseudo(const char *sysroot);

// Main API
//  1. multiple auv_run()
//  2. auv_apply()
//  3. auv_commit()
//  4. auv_finialize()
void auv_run(char *argv[], const char *overlay_dir);
int auv_apply(char *base, char *diff, char *target, int ignore_exist);
int auv_apply_dir(char *base, char *diff, char *target, char *_dir, int ignore_exist);
int auv_commit(char *base, char *target);
int auv_finialize(char *base, char *target);
int auv_finialize_dir(char *base, char *target);

// Helper function
int auv_mkdir(const char *path);
int auv_ensure_path_is_dir(const char *path);
int auv_exchange(char *src, char *dst);

int auv_lstat(const char *path, struct stat *st);

int auv_exist(const char *path);

int auv_ischar(struct stat *st);
int auv_isdir(struct stat *st);
int auv_issymlink(struct stat *st);

int auv_umount(const char *path);
