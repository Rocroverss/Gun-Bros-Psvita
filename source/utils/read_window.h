#ifndef GUNBROS_READ_WINDOW_H
#define GUNBROS_READ_WINDOW_H
#include <stdio.h>
#include <stdint.h>
/* Immutable, seekable native FILEs only. Four shared 64 KiB windows; 64 cursors.
 * Mixed stdio/descriptor operations must detach before bypassing this API. */
int gunbros_window_attach(FILE *file, int64_t length);
int gunbros_window_active(FILE *file);
size_t gunbros_window_read(void *dst, size_t size, size_t count, FILE *file);
int gunbros_window_seek(FILE *file, int64_t offset, int origin);
long gunbros_window_tell(FILE *file);
int gunbros_window_eof(FILE *file);
int gunbros_window_error(FILE *file);
int gunbros_window_detach(FILE *file);
void gunbros_window_drop(FILE *file);
void gunbros_window_rewind(FILE *file);
#endif
