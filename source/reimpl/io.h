/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  io.h
 * @brief Wrappers and implementations for some of the IO functions.
 */

#ifndef SOLOADER_IO_H
#define SOLOADER_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/dirent.h>
#include <sys/syslimits.h>
#include <sys/fcntl.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef DT_DIR
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#endif

typedef struct __attribute__((__packed__)) stat64_bionic {
    unsigned long long st_dev;
    unsigned char __pad0[4];
    unsigned long __st_ino;
    unsigned int st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    unsigned long long st_rdev;
    unsigned char __pad3[4];
    long long st_size;
    unsigned long st_blksize;
    unsigned long long st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    unsigned long long st_ino;
} stat64_bionic;

typedef struct __attribute__((__packed__)) dirent64_bionic {
    int16_t d_ino; // 2 bytes // offset 0x0
    int64_t d_off; // 8 bytes // offset 0x2
    uint64_t d_reclen; // 8 bytes // 0xA
    unsigned char d_type; // 1 byte // offset 0x12
    char d_name[256]; // 256 bytes // offset 0x13
} dirent64_bionic;

int open_soloader(const char * path, int oflag, ...);

int dup_soloader(int fd);

FILE * fopen_soloader(const char * filename, const char * mode);

FILE * fdopen_soloader(int fd, const char *mode);

void gunbros_track_fd_path(int fd, const char *path);

void gunbros_forget_fd_path(int fd);

DIR *opendir_soloader(char *name);

int stat_soloader(const char * path, stat64_bionic * buf);

int fstat_soloader(int fd, stat64_bionic * buf);

struct dirent64_bionic * readdir_soloader(DIR *dir);

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result);

int close_soloader(int fd);

int fclose_soloader(FILE *f);

int closedir_soloader(DIR *dir);

int fprintf_soloader(FILE *stream, const char *format, ...);

int vfprintf_soloader(FILE *stream, const char *format, va_list args);

void rewind_soloader(FILE *stream);

int fseeko_soloader(FILE *stream, off_t offset, int whence);

off_t ftello_soloader(FILE *stream);

int ungetc_soloader(int c, FILE *stream);

int setvbuf_soloader(FILE *stream, char *buffer, int mode, size_t size);

int fcntl_soloader(int fd, int cmd, ...);

int ioctl_soloader(int fd, int request, ... /* arg */);

int fsync_soloader(int fd);

#ifdef USE_SCELIBC_IO
size_t fread_soloader(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite_soloader(const void *ptr, size_t size, size_t count, FILE *stream);
int fseek_soloader(FILE *stream, long int offset, int origin);
long int ftell_soloader(FILE *stream);
int feof_soloader(FILE *stream);
int ferror_soloader(FILE *stream);
int fflush_soloader(FILE *stream);
char *fgets_soloader(char *s, int n, FILE *stream);
int fgetc_soloader(FILE *stream);
int getc_soloader(FILE *stream);
int fileno_soloader(FILE *stream);
int fgetpos_soloader(FILE *stream, fpos_t *pos);
int fsetpos_soloader(FILE *stream, const fpos_t *pos);
FILE *freopen_soloader(const char *path, const char *mode, FILE *stream);
#endif

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_IO_H
