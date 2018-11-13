#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wm/wm_view.h"
#include "wm/wm_seat.h"
#include "wm/wm_server.h"
#include "wm/wm.h"

/*
 * Callbacks: xdg_surface
 */
static void handle_xdg_map(struct wl_listener* listener, void* data){
    struct wm_view* view = wl_container_of(listener, view, map);
    view->mapped = true;
}

static void handle_xdg_unmap(struct wl_listener* listener, void* data){
    struct wm_view* view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    wm_callback_destroy_view(view);
}

static void handle_xdg_destroy(struct wl_listener* listener, void* data){
    struct wm_view* view = wl_container_of(listener, view, destroy);
    wm_view_destroy(view);
    free(view);
}


/*
 * Callbacks: xwayland_surface
 */
static void handle_xwayland_map(struct wl_listener* listener, void* data){
    struct wm_view* view = wl_container_of(listener, view, map);

    const char* title;
    const char* app_id; 
    const char* role;
    wm_view_get_info(view, &title, &app_id, &role);
    wlr_log(WLR_DEBUG, "New wm_view (xwayland): %s, %s, %s", title, app_id, role);
    wm_callback_init_view(view);

    view->mapped = true;
}

static void handle_xwayland_unmap(struct wl_listener* listener, void* data){
    struct wm_view* view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    wm_callback_destroy_view(view);
}

static void handle_xwayland_destroy(struct wl_listener* listener, void* data){
    struct wm_view* view = wl_container_of(listener, view, destroy);
    wm_view_destroy(view);
    free(view);
}


/*
 * Class implementation
 */
void wm_view_init_xdg(struct wm_view* view, struct wm_server* server, struct wlr_xdg_surface* surface){
    view->kind = WM_VIEW_XDG;

    view->wm_server = server;
    view->wlr_xdg_surface = surface;

    const char* title;
    const char* app_id; 
    const char* role;
    wm_view_get_info(view, &title, &app_id, &role);
    wlr_log(WLR_DEBUG, "New wm_view (xdg): %s, %s, %s", title, app_id, role);

    view->mapped = false;

    view->map.notify = &handle_xdg_map;
    wl_signal_add(&surface->events.map, &view->map);

    view->unmap.notify = &handle_xdg_unmap;
    wl_signal_add(&surface->events.unmap, &view->unmap);

    view->destroy.notify = &handle_xdg_destroy;
    wl_signal_add(&surface->events.destroy, &view->destroy);

    /*
     * XDG Views are initialized immediately to set the width/height before mapping
     */
    wm_callback_init_view(view);

    /* Get rid of white spaces around; therefore geometry.width/height should always equal current.width/height */
    wlr_xdg_toplevel_set_tiled(surface, 15);
}

void wm_view_init_xwayland(struct wm_view* view, struct wm_server* server, struct wlr_xwayland_surface* surface){
    view->kind = WM_VIEW_XWAYLAND;

    view->wm_server = server;
    view->wlr_xwayland_surface = surface;

    view->mapped = false;

    view->map.notify = &handle_xwayland_map;
    wl_signal_add(&surface->events.map, &view->map);

    view->unmap.notify = &handle_xwayland_unmap;
    wl_signal_add(&surface->events.unmap, &view->unmap);

    view->destroy.notify = &handle_xwayland_destroy;
    wl_signal_add(&surface->events.destroy, &view->destroy);

    /*
     * XWayland views are not initialised immediately, as there are a number of useless
     * surfaces, that never get mapped...
     */

}

void wm_view_destroy(struct wm_view* view){
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->link);
}

void wm_view_set_box(struct wm_view* view, double x, double y, double width, double height){
    view->display_x = x;
    view->display_y = y;
    view->display_width = width;
    view->display_height = height;
}

void wm_view_get_info(struct wm_view* view, const char** title, const char** app_id, const char** role){
    switch(view->kind){
    case WM_VIEW_XDG:
        *title = view->wlr_xdg_surface->toplevel->title;
        *app_id = view->wlr_xdg_surface->toplevel->app_id;
        *role = "toplevel";
        break;
    case WM_VIEW_XWAYLAND:
        *title = view->wlr_xwayland_surface->title;
        *app_id = view->wlr_xwayland_surface->class;
        *role = view->wlr_xwayland_surface->instance;
        break;
    }

}

void wm_view_request_size(struct wm_view* view, int width, int height){
    switch(view->kind){
    case WM_VIEW_XDG:
        if(!view->wlr_xdg_surface){
            wlr_log(WLR_DEBUG, "Warning: view with wlr_xdg_surface == 0");
            return;
        }

        if(view->wlr_xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL){
            wlr_xdg_toplevel_set_size(view->wlr_xdg_surface, width, height);
        }else{
            wlr_log(WLR_DEBUG, "Warning: Can only set size on toplevel");
        }
        break;
    case WM_VIEW_XWAYLAND:
        wlr_xwayland_surface_configure(view->wlr_xwayland_surface, 0, 0, width, height);
        break;
    }
}

void wm_view_get_size(struct wm_view* view, int* width, int* height){
    switch(view->kind){
    case WM_VIEW_XDG:
        /* Fixed by set_tiled */
        /* Although during updates not strictly equal? */
        /* assert(view->wlr_xdg_surface->geometry.width == view->wlr_xdg_surface->surface->current.width); */
        /* assert(view->wlr_xdg_surface->geometry.height == view->wlr_xdg_surface->surface->current.height); */

        if(!view->wlr_xdg_surface){
            *width = 0;
            *height = 0;

            wlr_log(WLR_DEBUG, "Warning: view with wlr_xdg_surface == 0");
            return;
        }

        *width = view->wlr_xdg_surface->geometry.width;
        *height = view->wlr_xdg_surface->geometry.height;
        break;
    case WM_VIEW_XWAYLAND:
        if(!view->wlr_xwayland_surface->surface){
            *width = 0;
            *height = 0;
            return;
        }

        *width = view->wlr_xwayland_surface->surface->current.width;
        *height = view->wlr_xwayland_surface->surface->current.height;
        break;
    }
}


void wm_view_focus(struct wm_view* view, struct wm_seat* seat){
    switch(view->kind){
    case WM_VIEW_XDG:
        wm_seat_focus_surface(seat, view->wlr_xdg_surface->surface);
        break;
    case WM_VIEW_XWAYLAND:
        if(!view->wlr_xwayland_surface->surface){
            return;
        }
        wm_seat_focus_surface(seat, view->wlr_xwayland_surface->surface);
        break;
    }
}

void wm_view_set_activated(struct wm_view* view, bool activated){
    switch(view->kind){
    case WM_VIEW_XDG:
        if(!view->wlr_xdg_surface){
            return;
        }
        wlr_xdg_toplevel_set_activated(view->wlr_xdg_surface, activated);
        break;
    case WM_VIEW_XWAYLAND:
        if(!view->wlr_xwayland_surface->surface){
            return;
        }
        wlr_xwayland_surface_activate(view->wlr_xwayland_surface, activated);
        break;
    }

}

struct wlr_surface* wm_view_surface_at(struct wm_view* view, double at_x, double at_y, double* sx, double* sy){
    switch(view->kind){
    case WM_VIEW_XDG:
        return wlr_xdg_surface_surface_at(view->wlr_xdg_surface, at_x, at_y, sx, sy);
    case WM_VIEW_XWAYLAND:
        if(!view->wlr_xwayland_surface->surface){
            return NULL;
        }

        return wlr_surface_surface_at(view->wlr_xwayland_surface->surface, at_x, at_y, sx, sy);
    }

    /* prevent warning */
    return NULL;
}

void wm_view_for_each_surface(struct wm_view* view, wlr_surface_iterator_func_t iterator, void* user_data){
    switch(view->kind){
    case WM_VIEW_XDG:
        wlr_xdg_surface_for_each_surface(view->wlr_xdg_surface, iterator, user_data);
        break;
    case WM_VIEW_XWAYLAND:
        if(!view->wlr_xwayland_surface->surface){
            return;
        }
        wlr_surface_for_each_surface(view->wlr_xwayland_surface->surface, iterator, user_data);
        break;
    }
}
