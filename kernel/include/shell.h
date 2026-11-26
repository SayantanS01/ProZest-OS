#ifndef PROZEST_SHELL_H
#define PROZEST_SHELL_H
#include "pzfs.h"
#define SHELL_LINE 96u
struct shell_keys { unsigned left,right,extended,skip; };
struct shell_line { char text[SHELL_LINE+1]; unsigned length,overflow; };
struct shell_io { void *ctx; void (*out)(void *,const char *); void (*clear)(void *); uint64_t managed,free; };
/* Returns ASCII, 8, 10 or zero (ignored scancode). */
unsigned shell_key(struct shell_keys *,uint8_t);
/* 0 ignored, 1 inserted, 2 erased, 3 submit, 4 overflow submit. */
unsigned shell_feed(struct shell_line *,unsigned);
/* Executes a mutable, NUL-terminated line of at most SHELL_LINE bytes. Returns 1 on exit. */
int shell_exec(struct pzfs *,struct shell_io *,char *);
#endif
