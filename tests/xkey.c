/* ARCADE-1 test tool — xkey: XTEST key injector for the play-window test.
 *
 * The play window (arcade1 run --live) takes its input from the focused X
 * window, so the only way to prove "walkdemo playable" end to end without a
 * human at the keyboard is to send REAL key events at it. This finds the
 * play window, focuses it, and runs a tiny key macro through the XTEST
 * extension — the same mechanism xdotool uses. Timing is wall-clock on
 * purpose: a live session is wall-clock.
 *
 *   xkey <window-name-substring> <class-substring|-> <word>...
 *
 * A word is an X keysym name (XStringToKeysym: "Right", "z", "Return",
 * "Escape", ...) optionally with ":<ms>" to hold it that long (default 120 =
 * a tap; a hold is press, wait, release). 80 ms gap between words.
 *
 * The class filter matters: a reparenting WM (GNOME/mutter) puts its own
 * frame window around the client, and the frame INHERITS THE TITLE — matching
 * the name alone focuses the decoration, and the keys go nowhere. The client
 * carries the application's class ("arcade1"), the frame its own
 * ("mutter-x11-frames"). Pass "-" to match any class.
 *
 * Focus is requested the EWMH way (_NET_ACTIVE_WINDOW, what a taskbar click
 * sends) and then with XSetInputFocus, verified, and re-verified after the
 * macro — a WM is free to move focus, and keys sent elsewhere are silent.
 *
 * Exit: 0 ok; 2 no display / no XTEST; 3 window not found in 5 s;
 *       4 could not keep focus for a key (it would go nowhere).
 *
 * TEST TOOLING ONLY: not built by `make check`, not part of the CLI. Built
 * on demand by tests/test_playwindow.sh, which skips cleanly when X11 or
 * libXtst are absent — hence the hand-declared XTEST prototypes (many
 * systems ship libXtst without its headers).
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* From libXtst — declared here so the test builds without <XTest.h>. */
extern int  XTestFakeKeyEvent(Display *dpy, unsigned int keycode,
                              int is_press, unsigned long delay);
extern Bool XTestQueryExtension(Display *dpy, int *ev, int *err,
                                int *maj, int *min);

/* Depth-first search for a mapped window whose name contains `name` and
 * (when `cls` is given) whose class contains `cls`. */
static Window find_win(Display *dpy, Window w, const char *name,
                       const char *cls, int depth)
{
    Window root, parent, *kids = NULL;
    unsigned nkids = 0, i;
    Window hit = None;
    char   *wname = NULL;
    XClassHint hint = { NULL, NULL };
    int      name_ok = 0, cls_ok = (cls == NULL);

    if (depth > 8)
        return None;
    if (XFetchName(dpy, w, &wname) && wname) {
        name_ok = strstr(wname, name) != NULL;
        XFree(wname);
    }
    if (name_ok && !cls_ok && XGetClassHint(dpy, w, &hint)) {
        cls_ok = (hint.res_class && strstr(hint.res_class, cls)) ||
                 (hint.res_name  && strstr(hint.res_name,  cls));
    }
    if (name_ok && cls_ok)
        hit = w;
    if (hint.res_class) XFree(hint.res_class);
    if (hint.res_name)  XFree(hint.res_name);
    if (hit != None)
        return hit;
    if (!XQueryTree(dpy, w, &root, &parent, &kids, &nkids))
        return None;
    for (i = 0; i < nkids && hit == None; i++)
        hit = find_win(dpy, kids[i], name, cls, depth + 1);
    if (kids)
        XFree(kids);
    return hit;
}

/* Ask the WM to activate the window (EWMH: the taskbar-click path). */
static void activate(Display *dpy, Window win)
{
    XClientMessageEvent cm;

    memset(&cm, 0, sizeof cm);
    cm.type         = ClientMessage;
    cm.window       = win;
    cm.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    cm.format       = 32;
    cm.data.l[0]    = 2;                /* source: pager/user tool       */
    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               (XEvent *)&cm);
}

int main(int argc, char **argv)
{
    Display *dpy;
    Window   win = None, focused = None;
    int      maj = 0, min = 0, ev = 0, err = 0, i;
    const char *cls;

    if (argc < 4) {
        fprintf(stderr,
                "usage: xkey <window-name-substring> <class-substring|-> "
                "<word>...\n");
        return 1;
    }
    cls = strcmp(argv[2], "-") ? argv[2] : NULL;
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "xkey: no X display\n");
        return 2;
    }
    if (!XTestQueryExtension(dpy, &ev, &err, &maj, &min)) {
        fprintf(stderr, "xkey: the XTEST extension is unavailable\n");
        XCloseDisplay(dpy);
        return 2;
    }

    /* The play window appears asynchronously after arcade1 starts; wait. */
    for (i = 0; i < 25 && win == None; i++) {
        win = find_win(dpy, DefaultRootWindow(dpy), argv[1], cls, 0);
        if (win == None)
            usleep(200000);
    }
    if (win == None) {
        fprintf(stderr, "xkey: no window matching '%s' / class '%s' after 5 s\n",
                argv[1], argv[2]);
        XCloseDisplay(dpy);
        return 3;
    }
    printf("window 0x%lx\n", (unsigned long)win);

    /* Focus it — XTEST keys go to the focused window. A WM may resist or
     * move focus itself, so verify and retry once. */
    for (i = 0; i < 2; i++) {
        activate(dpy, win);
        XRaiseWindow(dpy, win);
        XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
        XSync(dpy, False);
        usleep(300000);
        XGetInputFocus(dpy, &focused, &err);
        if (focused == win)
            break;
    }
    if (focused != win) {
        fprintf(stderr, "xkey: cannot focus 0x%lx (focus is 0x%lx)\n",
                (unsigned long)win, (unsigned long)focused);
        XCloseDisplay(dpy);
        return 4;
    }

    /* Settle: the WM's own focus bookkeeping can still be in flight (e.g.
     * mutter focuses whatever it likes when a previous window closes). Give
     * it a beat, re-verify, and re-assert once — silently losing focus
     * mid-macro sends the keys to another window and looks like a play-
     * window bug that isn't one. */
    for (i = 0; i < 2; i++) {
        usleep(300000);
        XSync(dpy, False);
        XGetInputFocus(dpy, &focused, &err);
        if (focused == win)
            break;
        activate(dpy, win);
        XRaiseWindow(dpy, win);
        XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
        XSync(dpy, False);
    }
    if (focused != win) {
        fprintf(stderr, "xkey: focus did not settle on 0x%lx (is 0x%lx)\n",
                (unsigned long)win, (unsigned long)focused);
        XCloseDisplay(dpy);
        return 4;
    }
    printf("focused\n");

    for (i = 3; i < argc; i++) {
        char  word[64];
        char *colon;
        int   hold = 120;
        KeySym ks;
        unsigned int code;

        /* The WM may move focus at any moment; re-assert before each word so
         * a tap is never sent into the void. */
        XGetInputFocus(dpy, &focused, &err);
        if (focused != win) {
            activate(dpy, win);
            XRaiseWindow(dpy, win);
            XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
            XSync(dpy, False);
            usleep(200000);
            XGetInputFocus(dpy, &focused, &err);
            if (focused != win) {
                fprintf(stderr, "xkey: focus is 0x%lx, not 0x%lx — key '%s' "
                        "would go nowhere\n", (unsigned long)focused,
                        (unsigned long)win, argv[i]);
                XCloseDisplay(dpy);
                return 4;
            }
            printf("refocused\n");
        }

        snprintf(word, sizeof word, "%s", argv[i]);
        if ((colon = strchr(word, ':')))
            hold = atoi(colon + 1), *colon = '\0';
        ks = XStringToKeysym(word);
        if (ks == NoSymbol) {
            fprintf(stderr, "xkey: unknown keysym '%s'\n", word);
            XCloseDisplay(dpy);
            return 1;
        }
        code = XKeysymToKeycode(dpy, ks);
        if (!code) {
            fprintf(stderr, "xkey: no keycode for '%s'\n", word);
            XCloseDisplay(dpy);
            return 1;
        }
        XTestFakeKeyEvent(dpy, code, True, CurrentTime);
        XSync(dpy, False);
        usleep((useconds_t)hold * 1000);
        XTestFakeKeyEvent(dpy, code, False, CurrentTime);
        XSync(dpy, False);
        printf("key %s %dms\n", word, hold);
        usleep(80000);
    }
    XSync(dpy, False);
    /* Drift AFTER the last key is harmless (the keys are already delivered);
     * say so rather than fail — the test's session assertions are the judge. */
    XGetInputFocus(dpy, &focused, &err);
    if (focused != win)
        fprintf(stderr, "xkey: note: focus moved to 0x%lx after the macro\n",
                (unsigned long)focused);
    XCloseDisplay(dpy);
    printf("done %d words\n", argc - 3);
    return 0;
}
