/* See LICENSE file for copyright and license details.
This wm is forked from dwm 6.7 (but keeps up with all dwm's updates), thanks suckless for their incredible work on dwm!
Infinite tags module is heavily inspired from 5element which is inspired from the hevel wayland compositor.

vxwm 2.3 (systraypatch) // by albaa

*/

// Modules configuration is in modules.h
// Config is in config.h

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "modules.h"
#include "drw.h"
#include "util.h"

/*
 * vxwm.c is the main window manager implementation for vxwm.
 * It handles X11 setup, event processing, layout management,
 * window rules, systray handling, and user interaction.
 */

/* Feature Sanity Checks: Enforce sub-dependencies based on enabled modules */
#if INFINITE_TAGS && !WINDOWMAP
    #undef WINDOWMAP
    #define WINDOWMAP 1 /* Infinite tags feature requires WINDOWMAP rendering mechanics */
#endif

#if ENHANCED_TOGGLE_FLOATING && !FLOATING_LAYOUT_FLOATS_WINDOWS
  #undef FLOATING_LAYOUT_FLOATS_WINDOWS
  #define FLOATING_LAYOUT_FLOATS_WINDOWS 1 /* Enhanced floating binds require layout compliance */
#endif

/* macros */
/* Common helper macros used throughout the window manager.
 * These simplify event masks, window dimensions, tag masks,
 * and visibility calculations.
 */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wy)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define SYSTEM_TRAY_REQUEST_DOCK    0

/* XEMBED protocol messages for Systray communication */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10
#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_DEACTIVATE    2
#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR

/* SHOWHIDEPROFILE defines how hidden windows are moved out of sight or unmapped */
#if !WINDOWMAP
  #if !PDWM_LIKE_TAGS_ANIMATION
    #if !SLOWER_TAGS_ANIMATION
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);  // Vanilla dwm behavior (offscreen push)
    #else
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, WIDTH(c) * -1, c->y);  // Slower vanilla offscreen push
    #endif
  #else
    #if !SLOWER_TAGS_ANIMATION
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, c->mon->wx + c->mon->ww / 2, -(HEIGHT(c) * 3) / 2);  // pdwm style top push
    #else
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, c->mon->wx + c->mon->ww / 2, -(HEIGHT(c)));  // Slower pdwm top push
    #endif
  #endif
#else
  /* If WINDOWMAP is enabled, unmap windows directly from the X Server instead of moving them offscreen */
  #define SHOWHIDEPROFILE         if (c->ismapped) { \
                                      window_unmap(dpy, c->win, root, 1); \
                                      c->ismapped = 0; \
                                  } 
#endif

/* Compile-time constant configuration wrapper for XRDB compatibility */
#if !XRDB
#define MAYBE_CONST const
#else
#define MAYBE_CONST
#endif

/* enums */
/* Enumerations used for cursors, color schemes, atoms, click targets,
 * and X11 protocol state management.
 */
enum { CurNormal, CurResize, CurMove,
#if BETTER_RESIZE && BR_CHANGE_CURSOR
       CurNW, CurNE, CurSW, CurSE,  // Corner resize cursors
       CurN, CurS, CurE, CurW,       // Edge resize cursors
#endif
       CurLast }; /* Cursors */

enum { SchemeNorm, SchemeSel }; /* Color schemes definitions */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation, NetSystemTrayOrientationHorz,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
#if !EWMH_TAGS
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* Extended Window Manager Hints (EWMH) Atoms */
#else 
       NetWMWindowTypeDialog, NetClientList, NetDesktopNames, NetDesktopViewport, NetNumberOfDesktops, NetCurrentDesktop, NetDesktopNum, NetLast }; 
#endif
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed specifications Atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* Standard ICCCM Window Manager Atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* UI Regions for click detection mappings */

/* Argument type used by key bindings and button actions.
 * It can carry an integer, unsigned integer, float, or pointer value.
 */
typedef union {
    int i;
    unsigned int ui;
    float f;
    const void *v;
} Arg;

typedef struct {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg *arg);
    const Arg arg;
} Button;

/* Forward declarations for Monitor and Client so that structs can refer to each other. */
typedef struct Monitor Monitor;
typedef struct Client Client;

/* Client structure stores information about a managed window.
 * It tracks geometry, size hints, window state, tags, and linkage
 * to the current monitor and client list.
 */
struct Client {
    char name[256];
    float mina, maxa;
    int x, y, w, h;
    int oldx, oldy, oldw, oldh;
    int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
    int bw, oldbw;
    unsigned int tags;
    int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
    Client *next;
    Client *snext;
    Monitor *mon;
    Window win;
#if INFINITE_TAGS
  int saved_cx, saved_cy; /* Tracks native coordinates independent of the viewport shifts */
  int saved_cw, saved_ch;
  int was_on_canvas;
#endif
#if WINDOWMAP
  int ismapped; /* Verification flag to prevent unnecessary X server map calls */
#endif
#if ENHANCED_TOGGLE_FLOATING
  int sfx, sfy, sfw, sfh; /* Restores previous dimensions when unfloating a window */
  #if RESTORE_SIZE_AND_POS_ETF
    int wasmanuallyedited;
  #endif
#endif 
};

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

typedef struct {
    const char *symbol;
    void (*arrange)(Monitor *);
} Layout;

#if INFINITE_TAGS
typedef struct {
    int cx, cy;
    int saved_cx, saved_cy;
} CanvasOffset;
#endif

struct Monitor {
    char ltsymbol[16];
    float mfact;
    int nmaster;
    int num;
    int by;               /* Bar geometry y-coordinate position */
    int mx, my, mw, mh;   /* Physical screen boundary size */
    int wx, wy, ww, wh;   /* Available window placement area (excluding bars/padding) */
#if GAPS
  int gappx;            /* Inner pixel spacing width between windows */
#endif
    unsigned int seltags;
    unsigned int sellt;
    unsigned int tagset[2];
    int showbar;
    int topbar;
    Client *clients;
    Client *sel;
    Client *stack;
    Monitor *next;
    Window barwin;
    const Layout *lt[2];
#if INFINITE_TAGS
  CanvasOffset *canvas;
#endif
#if EXTERNAL_BARS
  int strut_top, strut_bottom, strut_left, strut_right;
#endif
};

/* Rule structure defines automatic rules for new windows.
 * Rules can match class, instance, title, tags, floating state, and monitor.
 */
typedef struct {
    const char *class;
    const char *instance;
    const char *title;
    unsigned int tags;
    int isfloating;
    int monitor;
} Rule;

typedef struct Systray   Systray;
struct Systray {
    Window win;
    Client *icons;
};

/* function declarations */
/* Function prototypes for all internal event handlers, helpers,
 * and window management operations used in vxwm.
 */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static unsigned int getsystraywidth();
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static Monitor *systraytomon(Monitor *m);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static Client *wintosystrayicon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

#include "modules/vxwm_includes.h"
#include "config.h"

/* variables */
/* Global state for the window manager, including display handles,
 * monitors, selected client, colors, cursors, and atoms.
 */
static Systray *systray = NULL;
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* Bar rendering calculated height */
static int lrpad;            /* Total text layout alignment padding (Left + Right) */

#if BAR_PADDING
static int vp;               /* Dynamic vertical outer bar padding */
static int sp;               /* Dynamic side/horizontal outer bar padding */
#endif

static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;

/* X11 Event execution array map linking events to internal handlers */
static void (*handler[LASTEvent]) (XEvent *) = {
    [ButtonPress] = buttonpress,
    [ClientMessage] = clientmessage,
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify] = configurenotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [Expose] = expose,
    [FocusIn] = focusin,
    [KeyPress] = keypress,
    [MappingNotify] = mappingnotify,
    [MapRequest] = maprequest,
    [MotionNotify] = motionnotify,
    [PropertyNotify] = propertynotify,
    [ResizeRequest] = resizerequest,
    [UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "modules/vxwm_includes.c"

/* Compile-time static check guaranteeing tag layouts can map inside unsigned 32-bit fields */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
/* Apply window rules from config to a newly managed client.
 * This sets initial tags, floating state, and monitor assignment.
 */
void
applyrules(Client *c)
{
    const char *class, *instance;
    unsigned int i;
    const Rule *r;
    Monitor *m;
    XClassHint ch = { NULL, NULL };

    /* Initialize client default attributes */
    c->isfloating = 0;
    c->tags = 0;
    XGetClassHint(dpy, c->win, &ch);
    class    = ch.res_class ? ch.res_class : broken;
    instance = ch.res_name  ? ch.res_name  : broken;

    /* Evaluate rules defined inside config.h to find valid pattern matches */
    for (i = 0; i < LENGTH(rules); i++) {
        r = &rules[i];
        if ((!r->title || strstr(c->name, r->title))
        && (!r->class || strstr(class, r->class))
        && (!r->instance || strstr(instance, r->instance)))
        {
            c->isfloating = r->isfloating;
            c->tags |= r->tags;
            for (m = mons; m && m->num != r->monitor; m = m->next);
            if (m)
                c->mon = m;
        }
    }
    if (ch.res_class)
        XFree(ch.res_class);
    if (ch.res_name)
        XFree(ch.res_name);
    
    /* Assign fallback tags matching current viewing mask if rule did not target specific tags */
    c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

/* Apply size hints and constraints for a client window.
 * This enforces minimum/maximum sizes, aspect ratios, increments,
 * and keeps windows within monitor bounds.
 */
int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
    int baseismin;
    Monitor *m = c->mon;

    /* Enforce 1px window minimum size constraint */
    *w = MAX(1, *w);
    *h = MAX(1, *h);
    
    /* Edge case boundary clamp checks during active dragging/resizing interactions */
    if (interact) {
        if (*x > sw)
            *x = sw - WIDTH(c);
        if (*y > sh)
            *y = sh - HEIGHT(c);
        if (*x + *w + 2 * c->bw < 0)
            *x = 0;
        if (*y + *h + 2 * c->bw < 0)
            *y = 0;
    } else {
        if (*x >= m->wx + m->ww)
            *x = m->wx + m->ww - WIDTH(c);
        if (*y >= m->wy + m->wh)
            *y = m->wy + m->wh - HEIGHT(c);
        if (*x + *w + 2 * c->bw <= m->wx)
            *x = m->wx;
        if (*y + *h + 2 * c->bw <= m->wy)
            *y = m->wy;
    }
    if (*h < bh)
        *h = bh;
    if (*w < bh)
        *w = bh;
        
    /* Verify hardware/ICCCM terminal size hints match specific geometry rules */
    if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
        if (!c->hintsvalid)
            updatesizehints(c);
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = c->basew == c->minw && c->baseh == c->minh;
        if (!baseismin) { /* temporarily remove base dimensions */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for aspect limits */
        if (c->mina > 0 && c->maxa > 0) {
            if (c->maxa < (float)*w / *h)
                *w = *h * c->maxa + 0.5;
            else if (c->mina < (float)*h / *w)
                *h = *w * c->mina + 0.5;
        }
        if (baseismin) { /* increment calculation requires this */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for increment value */
        if (c->incw)
            *w -= *w % c->incw;
        if (c->inch)
            *h -= *h % c->inch;
        /* restore base dimensions */
        *w = MAX(*w + c->basew, c->minw);
        *h = MAX(*h + c->baseh, c->minh);
        if (c->maxw)
            *w = MIN(*w, c->maxw);
        if (c->maxh)
            *h = MIN(*h, c->maxh);
    }
    return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

/* Arrange one monitor or all monitors by showing/hiding clients
 * and then applying the current layout to each monitor.
 */
void
arrange(Monitor *m)
{
#if WINDOWMAP
    XGrabServer(dpy); /* Lock X Server commands to prevent window rendering flickers */
#endif
    if (m)
        showhide(m->stack);
    else for (m = mons; m; m = m->next)
        showhide(m->stack);
#if WINDOWMAP
    XUngrabServer(dpy);
    XSync(dpy, False);
#endif
    if (m) {
        arrangemon(m);
        restack(m);
    } else for (m = mons; m; m = m->next)
        arrangemon(m);
}

/* Run the layout function for a specific monitor.
 * This updates the layout symbol and calls the arrange callback.
 */
void
arrangemon(Monitor *m)
{
    strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol - 1);
    m->ltsymbol[sizeof m->ltsymbol - 1] = '\0';
    if (m->lt[m->sellt]->arrange)
        m->lt[m->sellt]->arrange(m);
}

/* Insert client at the beginning of the monitor's client list. */
void
attach(Client *c)
{
    c->next = c->mon->clients;
    c->mon->clients = c;
}

/* Insert client at the beginning of the stacked client list.
 * This list defines focus traversal and stacking order.
 */
void
attachstack(Client *c)
{
    c->snext = c->mon->stack;
    c->mon->stack = c;
}

/* Handle pointer button events from the bar, tags, status text,
 * and client windows. This determines click targets and executes
 * configured button actions.
 */
void
buttonpress(XEvent *e)
{
#if !OCCUPIED_TAGS_DECORATION
    unsigned int i, x, click;
#else
    unsigned int i, x, click, occ;
#endif
    Arg arg = {0};
    Client *c;
    Monitor *m;
    XButtonPressedEvent *ev = &e->xbutton;

    click = ClkRootWin;
    /* Safely switch target selected monitor focus if focus shifts across multi-head setups */
    if ((m = wintomon(ev->window)) && m != selmon) {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(NULL);
    }
    
    /* UI Navigation targets calculation depending on where user clicks inside the bar window */
    if (ev->window == selmon->barwin) {
#if !OCCUPIED_TAGS_DECORATION
        i = x = 0;
#else 
        i = x = occ = 0;
        /* Generate bitmask showing active tags containing client instances */
        for (c = m->clients; c; c = c->next)
            occ |= c->tags;
#endif
        do
#if !OCCUPIED_TAGS_DECORATION
            x += TEXTW(tags[i]);
#else 
            x += TEXTW(occ & 1 << i ? occupiedtags[i] : tags[i]);
#endif
        while (ev->x >= x && ++i < LENGTH(tags));
        if (i < LENGTH(tags)) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x < x + TEXTW(selmon->ltsymbol))
            click = ClkLtSymbol;
        else if (ev->x > selmon->ww - (int)TEXTW(stext) - getsystraywidth())
            click = ClkStatusText;
        else
            click = ClkWinTitle;
    } else if ((c = wintoclient(ev->window))) {
        focus(c);
        restack(selmon);
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }
    
    /* Loop definitions array maps matching executable bindings */
    for (i = 0; i < LENGTH(buttons); i++)
        if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
        && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
            buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

/* Detect whether another window manager has already grabbed
 * the root window. If so, vxwm aborts startup.
 */
void
checkotherwm(void)
{
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* Expecting BadAccess error here if another WM initialization grabbed the root window */
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}

/* Clean up all resources before exit: unmanage windows, destroy bars,
 * free memory and restore the input focus to the X server.
 */
void
cleanup(void)
{
    Arg a = {.ui = ~0};
    Layout foo = { "", NULL };
    Monitor *m;
    size_t i;

    view(&a);
    selmon->lt[selmon->sellt] = &foo;
    for (m = mons; m; m = m->next)
        while (m->stack)
            unmanage(m->stack, 0);
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    while (mons)
        cleanupmon(mons);

    /* Free Systray instances and destroy specific overlay window nodes */
    if (showsystray) {
        XUnmapWindow(dpy, systray->win);
        XDestroyWindow(dpy, systray->win);
        free(systray);
    }

    for (i = 0; i < CurLast; i++)
        drw_cur_free(drw, cursor[i]);
    for (i = 0; i < LENGTH(colors); i++)
        drw_scm_free(drw, scheme[i], 3);
    free(scheme);
    XDestroyWindow(dpy, wmcheckwin);
    drw_free(drw);
    XSync(dpy, False);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

/* Remove and free a monitor object when it is no longer needed.
 * This also destroys the monitor's bar window.
 */
void
cleanupmon(Monitor *mon)
{
    Monitor *m;

    if (mon == mons)
        mons = mons->next;
    else {
        for (m = mons; m && m->next != mon; m = m->next);
        m->next = mon->next;
    }
    XUnmapWindow(dpy, mon->barwin);
    XDestroyWindow(dpy, mon->barwin);
#if INFINITE_TAGS
  free(mon->canvas);
#endif
  free(mon);
}

/* Handle ClientMessage events from X11 windows.
 * This covers fullscreen requests, active window hints, and systray docking.
 */
void
clientmessage(XEvent *e)
{
    XWindowAttributes wa;
    XSetWindowAttributes swa;
    XClientMessageEvent *cme = &e->xclient;
    Client *c = wintoclient(cme->window);

    /* Handle specific XEMBED notifications demanding system tray embedding */
    if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
        if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
            if (!(c = (Client *)calloc(1, sizeof(Client))))
                die("fatal: could not malloc() %u bytes\n", sizeof(Client));
            if (!(c->win = cme->data.l[2])) {
                free(c);
                return;
            }
            c->mon = selmon;
            c->next = systray->icons;
            systray->icons = c;
            if (!XGetWindowAttributes(dpy, c->win, &wa)) {
                wa.width = bh;
                wa.height = bh;
                wa.border_width = 0;
            }
            c->x = c->oldx = c->y = c->oldy = 0;
            c->w = c->oldw = wa.width;
            c->h = c->oldh = wa.height;
            c->oldbw = wa.border_width;
            c->bw = 0;
            c->isfloating = True;
            c->tags = 1;
            updatesizehints(c);
            updatesystrayicongeom(c, wa.width, wa.height);
            XAddToSaveSet(dpy, c->win);
            XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
            XReparentWindow(dpy, c->win, systray->win, 0, 0);
            swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
            XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
            
            /* Core handshake initialization signals for XEmbed standard client tracking */
            sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0, systray->win, XEMBED_EMBEDDED_VERSION);
            sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0, systray->win, XEMBED_EMBEDDED_VERSION);
            sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
            sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0, systray->win, XEMBED_EMBEDDED_VERSION);
            XSync(dpy, False);
            resizebarwin(selmon);
            updatesystray();
            setclientstate(c, NormalState);
        }
        return;
    }

    if (!c)
        return;
    /* NetWM state validation properties (e.g. tracking applications requesting fullscreen) */
    if (cme->message_type == netatom[NetWMState]) {
        if (cme->data.l[1] == netatom[NetWMFullscreen]
        || cme->data.l[2] == netatom[NetWMFullscreen])
            setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
    } else if (cme->message_type == netatom[NetActiveWindow]) {
        if (c != selmon->sel && !c->isurgent)
            seturgent(c, 1);
    }
}

/* Send a synthetic ConfigureNotify event to the managed client.
 * This keeps the window's window manager hints synchronized.
 */
void
configure(Client *c)
{
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = c->win;
    ce.window = c->win;
    ce.x = c->x;
    ce.y = c->y;
    ce.width = c->w;
    ce.height = c->h;
    ce.border_width = c->bw;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

/* Respond to ConfigureNotify events from the root window.
 * This updates monitor geometry and adjusts bars and fullscreen clients.
 */
void
configurenotify(XEvent *e)
{
    Monitor *m;
    Client *c;
    XConfigureEvent *ev = &e->xconfigure;
    int dirty;

    if (ev->window == root) {
        dirty = (sw != ev->width || sh != ev->height);
        sw = ev->width;
        sh = ev->height;
        if (updategeom() || dirty) {
            drw_resize(drw, sw, bh);
            updatebars();
            for (m = mons; m; m = m->next) {
                for (c = m->clients; c; c = c->next)
                    if (c->isfullscreen)
                        resizeclient(c, m->mx, m->my, m->mw, m->mh);
#if !BAR_PADDING
                XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
#else
                XMoveResizeWindow(dpy, m->barwin, m->wx + sp, m->by + vp, m->ww -  2 * sp, bh);
#endif
            }
            focus(NULL);
            arrange(NULL);
        }
    }
}

/* Handle requests from windows to change their configuration.
 * For managed clients, this may update floating geometry or pass
 * unhandled requests through to XConfigureWindow.
 */
void
configurerequest(XEvent *e)
{
    Client *c;
    Monitor *m;
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    if ((c = wintoclient(ev->window))) {
        if (ev->value_mask & CWBorderWidth)
            c->bw = ev->border_width;
        else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
            m = c->mon;
            if (ev->value_mask & CWX) {
                c->oldx = c->x;
                c->x = m->mx + ev->x;
            }
            if (ev->value_mask & CWY) {
                c->oldy = c->y;
                c->y = m->my + ev->y;
            }
            if (ev->value_mask & CWWidth) {
                c->oldw = c->w;
                c->w = ev->width;
            }
            if (ev->value_mask & CWHeight) {
                c->oldh = c->h;
                c->h = ev->height;
            }
            /* Smart geometry adjustments keeping floating windows bound within display screens */
            if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
                c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* Center in X layout direction */
            if ((c->y + c->h) > m->my + m->mh && c->isfloating)
                c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* Center in Y layout direction */
            if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
                configure(c);
            if (ISVISIBLE(c))
                XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
        } else
            configure(c);
    } else {
        /* Standard structural overrides passing control directly down into generic X windows */
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
    XSync(dpy, False);
}

/* Create a new monitor structure with default layout and tag state.
 * This is called during startup and when monitor geometry changes.
 */
Monitor *
createmon(void)
{
    Monitor *m;

    m = ecalloc(1, sizeof(Monitor));
    m->tagset[0] = m->tagset[1] = 1;
    m->mfact = mfact;
    m->nmaster = nmaster;
    m->showbar = showbar;
    m->topbar = topbar;
#if GAPS
  m->gappx = gappx;
#endif
    m->lt[0] = &layouts[0];
    m->lt[1] = &layouts[1 % LENGTH(layouts)];
    strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
#if INFINITE_TAGS
  m->canvas = ecalloc(LENGTH(tags), sizeof(CanvasOffset));
  unsigned int i;
  for (i = 0; i < LENGTH(tags); i++) {
      m->canvas[i].cx = 0;
      m->canvas[i].cy = 0;
  }
#endif
    return m;
}

/* Handle DestroyNotify events for managed windows and systray icons.
 * When a client is destroyed, it is removed from management.
 */
void
destroynotify(XEvent *e)
{
    Client *c;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    if ((c = wintoclient(ev->window)))
        unmanage(c, 1);
    else if ((c = wintosystrayicon(ev->window))) {
        removesystrayicon(c);
        resizebarwin(selmon);
        updatesystray();
    }
#if EXTERNAL_BARS
  externalbars_unregister(ev->window);
#endif
}

/* Remove a client from the monitor's client list. */
void
detach(Client *c)
{
    Client **tc;

    for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
    *tc = c->next;
}

/* Remove a client from the stacked focus list.
 * If the removed client was selected, select the next visible client.
 */
void
detachstack(Client *c)
{
    Client **tc, *t;

    for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
    *tc = c->snext;

    if (c == c->mon->sel) {
        for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
        c->mon->sel = t;
    }
}

/* Return the monitor in the given direction relative to the selected monitor.
 * This is used for monitor switching and moving clients between screens.
 */
Monitor *
dirtomon(int dir)
{
    Monitor *m = NULL;

    if (dir > 0) {
        if (!(m = selmon->next))
            m = mons;
    } else if (selmon == mons)
        for (m = mons; m->next; m = m->next);
    else
        for (m = mons; m->next != selmon; m = m->next);
    return m;
}

/* Draw the status bar for a single monitor.
 * This includes tags, layout symbol, window title, and status text.
 */
void
drawbar(Monitor *m)
{
    int x, w, tw = 0, st_ ...
