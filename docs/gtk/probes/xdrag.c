// X11 counterpart of wldrag: press, move with the button held, release.
// Commands are given as arguments, the same grammar wldrag takes:
//
//     xdrag move 360 340 down sleep 400 move 400 380 sleep 200 up
//
// Coordinates are absolute screen pixels. Needs XTEST, so it does not work
// on Wayland -- that is what wldrag is for.
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void nap(int ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

int main(int argc, char** argv)
{
    Display* d = XOpenDisplay(NULL);
    if ( !d )
    {
        fprintf(stderr, "no display\n");
        return 1;
    }

    for ( int i = 1; i < argc; i++ )
    {
        if ( !strcmp(argv[i], "move") && i + 2 < argc )
        {
            int x = atoi(argv[i + 1]), y = atoi(argv[i + 2]);
            i += 2;
            XTestFakeMotionEvent(d, -1, x, y, 0);
            XFlush(d);
            printf("move %d,%d\n", x, y);
        }
        else if ( !strcmp(argv[i], "down") || !strcmp(argv[i], "up") )
        {
            int st = argv[i][0] == 'd';
            XTestFakeButtonEvent(d, 1, st, 0);
            XFlush(d);
            printf("%s\n", st ? "down" : "up");
        }
        else if ( !strcmp(argv[i], "sleep") && i + 1 < argc )
        {
            nap(atoi(argv[++i]));
        }
        else
        {
            fprintf(stderr, "bad: %s\n", argv[i]);
            return 2;
        }
    }

    XCloseDisplay(d);
    return 0;
}
