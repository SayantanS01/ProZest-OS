#ifndef PROZEST_WINDOW_H
#define PROZEST_WINDOW_H
#include "desktop.h"
#define WM_W 320u
#define WM_H 248u
#define WM_TITLE 30u
struct wm_window {size_t x,y;unsigned open;};
struct wm_state {size_t width,height;struct wm_window win[2];int top,drag;size_t ox,oy;};
int wm_init(struct wm_state *,size_t,size_t);
int wm_open(struct wm_state *,int);
void wm_home(struct wm_state *);
int wm_hit(const struct wm_state *,size_t,size_t);
int wm_down(struct wm_state *,size_t,size_t);
int wm_move(struct wm_state *,size_t,size_t);
void wm_up(struct wm_state *);
int wm_render(const struct desktop_surface *,const struct wm_state *,const struct desktop_info *,desktop_text,void *);
#endif
