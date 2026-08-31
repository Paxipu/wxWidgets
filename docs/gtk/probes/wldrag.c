// Drive a Wayland pointer well enough to perform a drag: unlike wlrctl, this
// holds the button down across motion. Commands are given as arguments:
//
//     wldrag move 360 340  down  move 400 380  move 500 450  up
//
// Coordinates are absolute screen pixels. The extent they are expressed in
// is the output's own size, which is asked for rather than assumed: a
// mismatch does not fail, it silently scales every coordinate, and the run
// then reports a drag that landed somewhere nobody aimed at.
#include <wayland-client.h>
#include "vp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BTN_LEFT 0x110

static struct wl_seat* seat;
static struct zwlr_virtual_pointer_manager_v1* mgr;
static struct wl_output* output;
static int outW, outH;

static void out_geometry(void*, struct wl_output*, int32_t, int32_t, int32_t,
                         int32_t, int32_t, const char*, const char*, int32_t)
{
}
static void out_mode(void*, struct wl_output*, uint32_t flags,
                     int32_t w, int32_t h, int32_t)
{
    if ( flags & WL_OUTPUT_MODE_CURRENT )
    {
        outW = w;
        outH = h;
    }
}
static void out_done(void*, struct wl_output*) {}
static void out_scale(void*, struct wl_output*, int32_t) {}
static void out_name(void*, struct wl_output*, const char*) {}
static void out_description(void*, struct wl_output*, const char*) {}
static const struct wl_output_listener out_l = {
    out_geometry, out_mode, out_done, out_scale, out_name, out_description
};

static void reg_global(void*, struct wl_registry* r, uint32_t name,
                       const char* iface, uint32_t ver)
{
    if ( !strcmp(iface, wl_seat_interface.name) )
        seat = static_cast<struct wl_seat*>(wl_registry_bind(r, name, &wl_seat_interface, 1));
    else if ( !strcmp(iface, wl_output_interface.name) && !output )
    {
        output = static_cast<struct wl_output*>(
                wl_registry_bind(r, name, &wl_output_interface,
                                 ver < 4 ? ver : 4));

        // Listen here rather than after the next roundtrip: the output's
        // mode arrives in the first dispatch after the bind, and a listener
        // added later never sees it.
        wl_output_add_listener(output, &out_l, NULL);
    }
    else if ( !strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name) )
        mgr = static_cast<struct zwlr_virtual_pointer_manager_v1*>(
                wl_registry_bind(r, name,
                    &zwlr_virtual_pointer_manager_v1_interface,
                    ver < 2 ? ver : 2));
}
static void reg_remove(void*, struct wl_registry*, uint32_t) {}
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

static uint32_t now(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(int argc, char** argv)
{
    struct wl_display* d = wl_display_connect(NULL);
    if ( !d ) { fprintf(stderr, "no wayland display\n"); return 1; }

    struct wl_registry* r = wl_display_get_registry(d);
    wl_registry_add_listener(r, &reg_l, NULL);
    wl_display_roundtrip(d);

    if ( !mgr ) { fprintf(stderr, "compositor has no zwlr_virtual_pointer\n"); return 1; }

    struct zwlr_virtual_pointer_v1* p =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, seat);
    wl_display_roundtrip(d);

    if ( output )
        wl_display_roundtrip(d);

    // An override stays available for a compositor whose output does not
    // announce a mode, but it is no longer how the size is normally learnt.
    if ( const char* e = getenv("WLDRAG_EXTENT") )
        sscanf(e, "%dx%d", &outW, &outH);

    if ( outW <= 0 || outH <= 0 )
    {
        fprintf(stderr, "no output size: set WLDRAG_EXTENT=WxH\n");
        return 1;
    }
    fprintf(stderr, "wldrag: extent %dx%d\n", outW, outH);

    for ( int i = 1; i < argc; ++i )
    {
        if ( !strcmp(argv[i], "move") && i + 2 < argc )
        {
            const int x = atoi(argv[i+1]), y = atoi(argv[i+2]); i += 2;
            zwlr_virtual_pointer_v1_motion_absolute(p, now(), x, y, outW, outH);
            zwlr_virtual_pointer_v1_frame(p);
            printf("move %d,%d\n", x, y);
        }
        else if ( !strcmp(argv[i], "down") || !strcmp(argv[i], "up") )
        {
            const int st = argv[i][0] == 'd' ? 1 : 0;
            zwlr_virtual_pointer_v1_button(p, now(), BTN_LEFT, st);
            zwlr_virtual_pointer_v1_frame(p);
            printf("%s\n", st ? "down" : "up");
        }
        else if ( !strcmp(argv[i], "sleep") && i + 1 < argc )
        {
            const int ms = atoi(argv[++i]);
            wl_display_flush(d);
            struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
            nanosleep(&t, NULL);
            continue;
        }
        else { fprintf(stderr, "bad command: %s\n", argv[i]); return 2; }

        wl_display_flush(d);
    }

    wl_display_roundtrip(d);
    zwlr_virtual_pointer_v1_destroy(p);
    wl_display_roundtrip(d);
    return 0;
}
