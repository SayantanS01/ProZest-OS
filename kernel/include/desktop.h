#ifndef PROZEST_DESKTOP_H
#define PROZEST_DESKTOP_H
#include <stdint.h>
#include <stddef.h>
#include "pzfs.h"
struct desktop_surface { volatile uint32_t *pixels; size_t width,height,stride; unsigned r,g,b; };
enum desktop_view { DESKTOP_HOME, DESKTOP_SYSTEM, DESKTOP_FILES, DESKTOP_TERMINAL };
struct desktop_info { uint64_t managed,free; const struct pzfs *fs; };
typedef void (*desktop_text)(void *,size_t,size_t,const char *,uint32_t);
int desktop_surface_valid(const struct desktop_surface *);
uint32_t desktop_rgb(const struct desktop_surface *,uint8_t,uint8_t,uint8_t);
void desktop_rect(const struct desktop_surface *,size_t,size_t,size_t,size_t,uint32_t);
enum desktop_view desktop_select(enum desktop_view,unsigned);
int desktop_render(const struct desktop_surface *,enum desktop_view,const struct desktop_info *,desktop_text,void *);
#endif
