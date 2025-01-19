/* See LICENSE file for copyright and license details.
 *
 * dz window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dzwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
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

#include "drw.h"
#include "util.h"

#define WM_MY_NAME "dzwm"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
				 * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISCURRENTWS(C)          (C->ws_idx == C->mon->ws_idx)
#define ISSHOWING(M,PI)         (M->selws->panes[PI].showing)
#define ISVISIBLE(C)            (ISCURRENTWS(C) && ISSHOWING(C->mon, C->pane_idx))
#define ISTILED(C,PI)           (ISCURRENTWS(C) && C->pane_idx == PI && !C->isfloating)
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define TEXTW_(X)               (drw_fontset_getwidth(drw, (X)))

#define BAR_INFO_WIN_TITLE      0
#define BAR_INFO_WS_OVERVIEW    1
#define BAR_INFO_CUSTOM         2

typedef unsigned int uint;

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { /* color schemes */
  SchemeNorm,
  SchemeNoClient,
  SchemeSel1,
  SchemeSel2,
  SchemeSel3,
  SchemeSel4,
  SchemeSel5,
  SchemeSel6,
  SchemeSel7,
  SchemeSel8,
  SchemeSel9,
  SchemeSel10,
  SchemeSel11,
  SchemeSel12,
  SchemeWS,
  SchemeStats,
  SchemeDivRatio,
  SchemeBarInfo,
  SchemeDate1,
  SchemeDate2,
  SchemeDate3,
  SchemeDate4,
};
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
  int i;
  uint ui;
  float f;
  const void *v;
  struct {
    int idx;
    int alt;
  } ws;
} Arg;

typedef struct {
  unsigned int click;
  unsigned int mask;
  unsigned int button;
  void (*func)(const Arg *arg);
  const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
  char name[256];
  float mina, maxa;
  int x, y, w, h;
  int oldx, oldy, oldw, oldh;
  int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
  int bw, oldbw;
  uint ws_idx;
  uint pane_idx;
  int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, ismaximized;
  int origx, origy, origw, origh;
  int is_arranged;
  Client *next;
  Client *snext;
  Monitor *mon;
  Window win;
};

typedef struct {
  unsigned int mod;
  KeySym keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

typedef struct Rect Rect;
typedef struct {
  const char *symbol;
  void (*arrange)(Monitor *, uint pi, Rect *);
} Layout;

struct Rect {
  uint x;
  uint y;
  uint w;
  uint h;
};

typedef struct {
  const char *class;
  const char *instance;
  const char *title;
  int ws_idx;
  int isfloating;
  int monitor;
} Rule;

typedef struct {
  uint x;
  uint sy;
} RenderData;

typedef void (*BarInfoRender)(RenderData *d);

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void centerwindow(const Arg *arg);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clearpanes(const Arg *arg);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void cyclelayout(const Arg *arg);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static void drawbar(Monitor *m);
void drawbar_status(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focuscycle(const Arg *arg);
static void focuspane(const Arg *arg);
static void focuspane_showing(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void inc_div_ratio(const Arg *arg);
static void inc_info_idx(const Arg *arg);
static void inc_max_disp(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void maximize(const Arg *arg);
static void motionnotify(XEvent *e);
static void moveclient_pane(const Arg *arg);
static void moveclient_paneidx(const Arg *arg);
static void moveclient_ws(const Arg *arg);
void moveclient(Client *, int x, int y, int w, int c);
static void movemouse(const Arg *arg);
static void movestack(const Arg *arg);
static Client *nexttiled(Client *c, uint pi);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setbarinfoidx(const Arg *arg);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static void switchworkspace(const Arg *arg);
static void tile_v(Monitor *m, uint pi, Rect *r);
static void tile_h(Monitor *m, uint pi, Rect *r);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglepane(const Arg *arg);
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
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
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
  [UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

#define WS_LEN       LENGTH(wsnames)
#define WS_PANES     LENGTH(panenames)
#define WS_ALTS      LENGTH(altnames)
#define BAR_INFO_CNT LENGTH(barinfonames)

typedef struct {
  uint showing;
  uint max_display;
  uint layout_idx;
} Pane;

typedef struct {
  Pane panes[WS_PANES];
  uint selpane;
  uint div_ratio;
} Workspace;

struct Monitor {
  Workspace workspaces[WS_LEN][WS_ALTS];
  uint ws_idx;
  uint last_ws_idx;
  uint alt_idx;
  uint last_alt_idx;
  Workspace *selws;
  uint bar_info_idx;
  int num;
  int by;               /* bar geometry */
  int mx, my, mw, mh;   /* screen size */
  int wx, wy, ww, wh;   /* window area  */
  int showbar;
  int topbar;
  uint status_x, status_y;
  Client *clients;
  Client *sel;
  Client *stack;
  Monitor *next;
  Window barwin;
};

/* function implementations */
void
applyrules(Client *c)
{
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = { NULL, NULL };

  /* rule matching */
  c->isfloating = 0;
  XGetClassHint(dpy, c->win, &ch);
  class    = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name  ? ch.res_name  : broken;

  c->ws_idx = c->mon->ws_idx;
  c->pane_idx = c->mon->selws->selpane;
  for (i = 0; i < LENGTH(rules); i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title))
	&& (!r->class || strstr(class, r->class))
	&& (!r->instance || strstr(instance, r->instance)))
      {
	c->isfloating = r->isfloating;
	if (r->ws_idx >= 0 && r->ws_idx < WS_LEN)
	  c->ws_idx = r->ws_idx;
	for (m = mons; m && m->num != r->monitor; m = m->next);
	if (m)
	  c->mon = m;
      }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
  int baseismin;
  Monitor *m = c->mon;

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);
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
  if (resizehints || c->isfloating) {
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

void
arrange(Monitor *m)
{
  if (m) {
    showhide(m->stack);
    arrangemon(m);
    restack(m);
  } else
    for (m = mons; m; m = m->next) {
      showhide(m->stack);
      arrangemon(m);
    }
}

void
arrangemon(Monitor *m)
{
  const Workspace *ws = m->selws;

  uint tiled_cnt[WS_PANES] = {0};
  for (Client *c = m->clients; c; c = c->next) {
    c->is_arranged = 0;

    if (c->isfloating
	|| c->ws_idx != m->ws_idx
	|| !ISSHOWING(m, c->pane_idx))
      continue;

    tiled_cnt[c->pane_idx]++;
  }

  uint div_cnt = 0;
  for (int i = 0; i < WS_PANES; i++) {
    if (tiled_cnt[i] > 0)
      div_cnt++;
  }
  Rect r = {
    .x = m->wx,
    .y = m->wy,
    .w = m->ww,
    .h = m->wh,
  };
  if (div_cnt == 1) {
    for (int i = 0; i < WS_PANES; i++) {
      if (!ISSHOWING(m, i) || tiled_cnt[i] == 0) continue;

      layouts[ws->panes[i].layout_idx].arrange(m, i, &r);
      break;
    }
  } else if (div_cnt > 1) {
    for (int i = 0, j = 0; i < WS_PANES; i++) {
      if (!ISSHOWING(m, i) || tiled_cnt[i] == 0) continue;

      switch (j) {
      case 0:
	r.w = m->ww * ws->div_ratio / 100;
	break;
      case 1:
	r.x += r.w;
	r.w = m->ww - r.w;
	break;
      }
      j++;

      layouts[ws->panes[i].layout_idx].arrange(m, i, &r);
    }
  }
}

void
attach(Client *c)
{
  c->next = c->mon->clients;
  c->mon->clients = c;
}

void
attachstack(Client *c)
{
  c->snext = c->mon->stack;
  c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
  unsigned int i, click;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = wintomon(ev->window)) && m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  if (ev->window == selmon->barwin) {
  } else if ((c = wintoclient(ev->window))) {
    focus(c);
    restack(selmon);
    XAllowEvents(dpy, ReplayPointer, CurrentTime);
    click = ClkClientWin;
  }
  for (i = 0; i < LENGTH(buttons); i++)
    if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
	&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
      buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
centerwindow(const Arg *arg)
{
  Client *c = selmon->sel;
  if (c && c->isfloating) {
    uint maxw = selmon->ww - 2 * c->bw;
    uint maxh = selmon->wh - 2 * c->bw;
    uint w = MIN(c->w, maxw);
    uint h = MIN(c->h, maxh);
    uint x = selmon->wx + (maxw - w) / 2;
    uint y = selmon->wy + (maxh - h) / 2;

    moveclient(c, x, y, w, h);
  }
}

void
checkotherwm(void)
{
  xerrorxlib = XSetErrorHandler(xerrorstart);
  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
}

void
cleanup(void)
{
  Monitor *m;
  size_t i;

  for (m = mons; m; m = m->next) {
    while (m->stack)
      unmanage(m->stack, 0);
  }
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  while (mons)
    cleanupmon(mons);
  for (i = 0; i < CurLast; i++)
    drw_cur_free(drw, cursor[i]);
  for (i = 0; i < LENGTH(colors); i++)
    free(scheme[i]);
  free(scheme);
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

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
  free(mon);
}

void
clearpanes(const Arg *arg)
{
  Workspace *ws = selmon->selws;
  ws->selpane = 0;
  ws->div_ratio = div_ratio_init;
  for (int i = 0; i < WS_PANES; i++) {
    ws->panes[i].showing = 0;
    ws->panes[i].max_display = max_disp_init;
    ws->panes[i].layout_idx = 0;
  }
  unfocus(selmon->sel, 0);
  selmon->sel = NULL;
  arrange(selmon);
  drawbar(selmon);
}

void
clientmessage(XEvent *e)
{
  XClientMessageEvent *cme = &e->xclient;
  Client *c = wintoclient(cme->window);

  if (!c)
    return;
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

void
configurenotify(XEvent *e)
{
  Monitor *m;
  Client *c;
  XConfigureEvent *ev = &e->xconfigure;
  int dirty;

  /* TODO: updategeom handling sucks, needs to be simplified */
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
	XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
      }
      focus(NULL);
      arrange(NULL);
    }
  }
}

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
    else if (c->isfloating) {
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
      if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
	c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
      if ((c->y + c->h) > m->my + m->mh && c->isfloating)
	c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
      if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
	configure(c);
      if (ISVISIBLE(c))
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    } else
      configure(c);
  } else {
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

Monitor *
createmon(void)
{
  Monitor *m;

  m = ecalloc(1, sizeof(Monitor));
  m->ws_idx = m->last_ws_idx = 0;
  m->alt_idx = m->last_alt_idx = 0;
  m->selws = &m->workspaces[0][0];
  for (int i = 0; i < WS_LEN; i++) {
    for (int j = 0; j < WS_ALTS; j++) {
      Workspace *ws = &m->workspaces[i][j];
      ws->selpane = 0;
      ws->div_ratio = div_ratio_init;
      for (int k = 0; k < WS_PANES; k++) {
	ws->panes[k].showing = 0;
	ws->panes[k].max_display = max_disp_init;
	ws->panes[k].layout_idx = 0;
      }
    }
  }
  m->showbar = showbar;
  m->topbar = topbar;
  m->bar_info_idx = 0;
  m->status_x = 0;
  m->status_y = 3;
  return m;
}

void
cyclelayout(const Arg *arg)
{
  if (!arg) return;

  Workspace *ws = selmon->selws;
  if (ws->selpane >= WS_PANES) return;

  Pane *p = &ws->panes[ws->selpane];
  if (arg->i > 0) {
    p->layout_idx++;
    p->layout_idx %= LENGTH(layouts);
  } else {
    if (p->layout_idx == 0)
      p->layout_idx = LENGTH(layouts) - 1;
    else
      p->layout_idx--;
  }

  arrange(selmon);
}

void
destroynotify(XEvent *e)
{
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = wintoclient(ev->window)))
    unmanage(c, 1);
}

void
detach(Client *c)
{
  Client **tc;

  for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
  *tc = c->next;
}

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

void
drawbar(Monitor *m)
{
  if (!m->showbar) return;

  const Workspace *ws = m->selws;

  uint cnt_all = 0, cnt_ws = 0, occ[WS_LEN] = {0}, cnt[WS_PANES] = {0};
  for (Client *c = m->clients; c; c = c->next) {
    cnt_all++;

    if (c->ws_idx == m->ws_idx)
      cnt[c->pane_idx]++;

    if (!occ[c->ws_idx]) {
      occ[c->ws_idx] = 1;
      cnt_ws++;
    }
  }

  uint x = 0, w = 0;
  char buf[30];

  {
    snprintf(buf, sizeof(buf), "%s%s", altnames[m->alt_idx], wsnames[m->ws_idx]);
    w = TEXTW(buf);
    drw_setscheme(drw, scheme[SchemeWS]);
    drw_text(drw, x, 0, w, bh, lrpad / 2, buf, 1);
    x += w;

    snprintf(buf, sizeof buf, "%d / %d", cnt_all, cnt_ws);
    w = TEXTW(buf);
    drw_setscheme(drw, scheme[SchemeStats]);
    drw_text(drw, x, 0, w, bh, lrpad / 2, buf, 0);
    x += w;

    snprintf(buf, sizeof(buf), "0.%d", ws->div_ratio);
    w = TEXTW_(buf) + lrpad / 2;
    drw_setscheme(drw, scheme[SchemeDivRatio]);
    drw_text(drw, x, 0, w, bh, 0, buf, 0);
    x += w;
  }

  const uint xx = m->wx + m->ww;
  for (int i = 0; i < WS_PANES && x < xx; i++) {
    const uint orig_x = x;
    drw_setscheme(drw, scheme[SchemeSel1 + i]);
    if (ISSHOWING(m, i)) {
      const char *p = panenames[i];
      w = TEXTW(p);
      if (x + w > xx) w = xx - x;

      drw_text(drw, x, 0, w, bh, lrpad / 2, p, 0);
      x += w;
    } else {
      w = 0;
    }

    uint w2 = 0;
    if (x + w < xx) {
      snprintf(buf, sizeof(buf), "%d / %d  (%s)",
	       cnt[i],
	       ws->panes[i].max_display,
	       layouts[ws->panes[i].layout_idx].symbol);
      w2 = TEXTW(buf);
      if (x + w2 > xx) w2 = xx - x;

      drw_setscheme(drw, scheme[cnt[i] ? SchemeNorm : SchemeNoClient]);
      drw_text(drw, x, 0, w2, bh, lrpad / 2, buf, 0);
      x += w2;
    }

    if (i == ws->selpane && w + w2 > 0) {
      drw_setscheme(drw, scheme[SchemeSel1 + i]);
      drw_rect(drw, orig_x, bh - 2, w + w2, 2, 1, 0);
    }
  }

  drw_map(drw, m->barwin, 0, 0, x, bh);
  m->status_x = x;
  drawbar_status(m);
}

void
drawbar_status(Monitor *m)
{
  if (!m->showbar) return;

  RenderData rd = {.x = m->wx + m->ww, .sy = m->status_y};
  if (m->bar_info_idx < LENGTH(barinfonames)) {
    const char *p = barinfonames[m->bar_info_idx];
    uint w = TEXTW_(p) + 7;
    if (m->status_x + w > rd.x) return;

    rd.x -= w;
    drw_setscheme(drw, scheme[SchemeBarInfo]);
    drw_rect(drw, rd.x, 0, w, rd.sy, 1, 1);
    drw_text(drw, rd.x, rd.sy, w, bh - rd.sy, 4, p, 0);
  }

  switch (m->bar_info_idx) {
  case BAR_INFO_WS_OVERVIEW:
    if (m->clients) {
      uint occ[WS_LEN] = {0}, urg[WS_LEN] = {0};
      for (Client *c = m->clients; c; c = c->next) {
	occ[c->ws_idx] = 1;
	if (c->isurgent) urg[c->ws_idx] = 1;
      }
      for (int i = WS_LEN - 1; i >= 0; i--) {
	if (!occ[i]) continue;

	uint w = TEXTW(wsnames[i]);
	if (m->status_x + w > rd.x) break;

	rd.x -= w;
	drw_text(drw, rd.x, 0, w, bh, lrpad / 2, wsnames[i], urg[i]);
      }
    }
    break;
  case BAR_INFO_WIN_TITLE:
    if (m->sel) {
      Client *c = m->sel;
      char buf[300];
      snprintf(buf, sizeof buf, "%s%s",
	       c->isfloating ? "🪽  " : "", c->name);
      uint w = MIN(MAX(TEXTW(buf), BAR_CLIENT_MIN_WIDTH), rd.x - m->status_x);

      size_t si = SchemeSel1 + c->pane_idx;
      drw_setscheme(drw, scheme[si]);
      rd.x -= w;
      drw_text(drw, rd.x, 0, w, bh, lrpad / 2, buf, 1);
    }
    break;
  default:
    {
      const uint i = m->bar_info_idx - BAR_INFO_CUSTOM;
      if (i < LENGTH(barinforenders)) {
	uint orig_x = rd.x;
	BarInfoRender r = barinforenders[i];
	r(&rd);

	drw_rect(drw, rd.x, 0, orig_x - rd.x, rd.sy, 1, 1);
      }
    }
  }

  if (m->status_x < rd.x) {
    drw_setscheme(drw, scheme[SchemeBarInfo]);
    drw_rect(drw, m->status_x, 0, rd.x - m->status_x, bh, 1, 1);
  }
  drw_map(drw, m->barwin, m->status_x, 0, m->wx + m->ww - m->status_x, bh);
}

void
drawbars(void)
{
  for (Monitor *m = mons; m; m = m->next)
    drawbar(m);
}

void
enternotify(XEvent *e)
{
  Client *c;
  Monitor *m;
  XCrossingEvent *ev = &e->xcrossing;

  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
    return;
  c = wintoclient(ev->window);
  m = c ? c->mon : wintomon(ev->window);
  if (m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
  } else if (!c || c == selmon->sel)
    return;
  focus(c);
}

void
expose(XEvent *e)
{
  Monitor *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count == 0 && (m = wintomon(ev->window)))
    if (m->bar_info_idx == BAR_INFO_WIN_TITLE)
      drawbar_status(m);
}

void
focus(Client *c)
{
  if (!c || !ISVISIBLE(c)) {
    c = selmon->stack;
    for (; c && !ISVISIBLE(c); c = c->snext);
  }
  if (selmon->sel && selmon->sel != c)
    unfocus(selmon->sel, 0);
  if (c) {
    if (c->mon != selmon)
      selmon = c->mon;
    if (c->isurgent)
      seturgent(c, 0);
    detachstack(c);
    attachstack(c);
    grabbuttons(c, 1);

    size_t si = SchemeSel1 + c->pane_idx;
    XSetWindowBorder(dpy, c->win, scheme[si][ColBorder].pixel);
    setfocus(c);
  } else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  selmon->sel = c;
  if (c && c->mon->bar_info_idx < BAR_INFO_CUSTOM)
    drawbar_status(c->mon);
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel && ev->window != selmon->sel->win)
    setfocus(selmon->sel);
}

void
focuscycle(const Arg *arg)
{
  if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
    return;

  const uint pi = selmon->sel->pane_idx;
  if (!ISSHOWING(selmon, pi))
    return;

  Client *c = NULL, *i;

  if (arg->i > 0) {
    for (c = selmon->sel->next; c; c = c->next)
      if (ISCURRENTWS(c) && c->pane_idx == pi)
	break;
    if (!c)
      for (c = selmon->clients; c; c = c->next)
	if (ISCURRENTWS(c) && c->pane_idx == pi)
	  break;
  } else {
    for (i = selmon->clients; i != selmon->sel; i = i->next)
      if (ISCURRENTWS(i) && i->pane_idx == pi)
	c = i;
    if (!c)
      for (i = i->next; i; i = i->next)
	if (ISCURRENTWS(i) && i->pane_idx == pi)
	  c = i;
  }

  if (c && c != selmon->sel) {
    focus(c);
    restack(selmon);
  }
}

void
focuspane_to(uint i)
{
  Workspace *ws = selmon->selws;
  ws->selpane = i;

  Client *c = NULL;
  if (ISSHOWING(selmon, ws->selpane)) {
    for (c = selmon->stack; c; c = c->snext)
      if (ISCURRENTWS(c) && c->pane_idx == ws->selpane)
	break;
  }

  if (c && c != selmon->sel) {
    focus(c);
    restack(selmon);
  }
  drawbar(selmon);
}

void
focuspane(const Arg *arg)
{
  Workspace *ws = selmon->selws;
  int i = ws->selpane + (arg->i > 0 ? 1 : -1);
  if (i < 0)
    i = WS_PANES - 1;
  else if (i >= WS_PANES)
    i = 0;

  focuspane_to(i);
}

void
focuspane_showing(const Arg *arg)
{
  Workspace *ws = selmon->selws;
  const uint orig_pi = ws->selpane;
  const int inc = arg->i > 0 ? 1 : -1;

  int i = orig_pi + inc;
  for (; i != orig_pi; i += inc) {
    if (i < 0)
      i = WS_PANES - 1;
    else if (i >= WS_PANES)
      i = 0;

    if (ws->panes[i].showing)
      break;
  }
  if (i == orig_pi) return;

  focuspane_to(i);
}

Atom
getatomprop(Client *c, Atom prop)
{
  int di;
  unsigned long dl;
  unsigned char *p = NULL;
  Atom da, atom = None;
  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
			 &da, &di, &dl, &dl, &p) == Success && p) {
    atom = *(Atom *)p;
    XFree(p);
  }
  return atom;
}

int
getrootptr(int *x, int *y)
{
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;
  if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
			 &real, &format, &n, &extra, (unsigned char **)&p) != Success)
    return -1;
  if (n != 0)
    result = *p;
  XFree(p);
  return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
  char **list = NULL;
  int n;
  XTextProperty name;

  if (!text || size == 0)
    return 0;
  text[0] = '\0';
  if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
    return 0;
  if (name.encoding == XA_STRING) {
    strncpy(text, (char *)name.value, size - 1);
  } else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
    strncpy(text, *list, size - 1);
    XFreeStringList(list);
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return 1;
}

void
grabbuttons(Client *c, int focused)
{
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    if (!focused)
      XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
		  BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
    for (i = 0; i < LENGTH(buttons); i++)
      if (buttons[i].click == ClkClientWin)
	for (j = 0; j < LENGTH(modifiers); j++)
	  XGrabButton(dpy, buttons[i].button,
		      buttons[i].mask | modifiers[j],
		      c->win, False, BUTTONMASK,
		      GrabModeAsync, GrabModeSync, None, None);
  }
}

void
grabkeys(void)
{
  updatenumlockmask();
  {
    unsigned int i, j, k;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    int start, end, skip;
    KeySym *syms;

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XDisplayKeycodes(dpy, &start, &end);
    syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
    if (!syms)
      return;
    for (k = start; k <= end; k++)
      for (i = 0; i < LENGTH(keys); i++)
	/* skip modifier codes, we do that ourselves */
	if (keys[i].keysym == syms[(k - start) * skip])
	  for (j = 0; j < LENGTH(modifiers); j++)
	    XGrabKey(dpy, k,
		     keys[i].mod | modifiers[j],
		     root, True,
		     GrabModeAsync, GrabModeAsync);
    XFree(syms);
  }
}

void
inc_div_ratio(const Arg *arg)
{
  if (!arg) return;

  Workspace *ws = selmon->selws;
  if (ws->selpane >= WS_PANES) return;

  uint d = ws->div_ratio + arg->i;
  if (d < 5 || d > 95) return;

  ws->div_ratio = d;
  drawbar(selmon);
  arrange(selmon);
}

void
inc_info_idx(const Arg *arg)
{
  if (!arg) return;

  int i = selmon->bar_info_idx + (arg->i > 0 ? 1 : -1);
  if (i < 0)
    i = BAR_INFO_CNT - 1;
  else if (i >= BAR_INFO_CNT)
    i = 0;

  selmon->bar_info_idx = i;
  drawbar_status(selmon);
}

void
inc_max_disp(const Arg *arg)
{
  if (!arg) return;

  Workspace *ws = selmon->selws;

  int i = ws->panes[ws->selpane].max_display +
    (arg->i > 0 ? 1 : -1);
  if (i < 0) return;

  ws->panes[ws->selpane].max_display = i;
  drawbar(selmon);
  arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
  while (n--)
    if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
	&& unique[n].width == info->width && unique[n].height == info->height)
      return 0;
  return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
  unsigned int i;
  KeySym keysym;
  XKeyEvent *ev;

  ev = &e->xkey;
  keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
  for (i = 0; i < LENGTH(keys); i++)
    if (keysym == keys[i].keysym
	&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
	&& keys[i].func)
      keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg)
{
  if (!selmon->sel)
    return;
  if (!sendevent(selmon->sel, wmatom[WMDelete])) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selmon->sel->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

void
manage(Window w, XWindowAttributes *wa)
{
  Client *c, *t = NULL;
  Window trans = None;
  XWindowChanges wc;

  c = ecalloc(1, sizeof(Client));
  c->win = w;
  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;

  updatetitle(c);
  if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
    c->mon = t->mon;
    c->ws_idx = t->ws_idx;
    c->pane_idx = t->pane_idx;
  } else {
    c->mon = selmon;
    applyrules(c);
  }

  if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
    c->x = c->mon->wx + c->mon->ww - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
    c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->wx);
  c->y = MAX(c->y, c->mon->wy);
  c->bw = borderpx;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);
  XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
  grabbuttons(c, 0);
  if (!c->isfloating)
    c->isfloating = c->oldstate = trans != None || c->isfixed;
  if (c->isfloating)
    XRaiseWindow(dpy, c->win);
  attach(c);
  attachstack(c);
  XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		  (unsigned char *) &(c->win), 1);
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
  setclientstate(c, NormalState);
  if (c->mon == selmon)
    unfocus(selmon->sel, 0);
  c->mon->sel = c;
  arrange(c->mon);
  XMapWindow(dpy, c->win);
  focus(NULL);
}

void
mappingnotify(XEvent *e)
{
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    grabkeys();
}

void
maprequest(XEvent *e)
{
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;

  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

void
maximize(const Arg *arg)
{
  Client *c = selmon->sel;
  if (!c || !c->isfloating) return;

  switch (c->ismaximized) {
  case 1:
    resize(c, selmon->wx, selmon->wy, selmon->mw - 2 * c->bw, selmon->mh - 2 * c->bw, 0);
    c->ismaximized = 2;
    break;
  case 2:
    resize(c, c->origx, c->origy, c->origw, c->origh, 0);
    c->ismaximized = 0;
    break;
  default:
    c->origx = c->x;
    c->origy = c->y;
    c->origw = c->w;
    c->origh = c->h;
    resize(c, selmon->wx, selmon->wy, selmon->ww - 2 * c->bw, selmon->wh - 2 * c->bw, 0);
    c->ismaximized = 1;
  }
}

void
motionnotify(XEvent *e)
{
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != root)
    return;
  if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  mon = m;
}

void
moveclient_pane(const Arg *arg)
{
  if (!selmon->sel) return;

  Workspace *ws = selmon->selws;
  int i = selmon->sel->pane_idx + (arg->i > 0 ? 1 : -1);
  if (i < 0)
    i = WS_PANES - 1;
  else if (i >= WS_PANES)
    i = 0;

  selmon->sel->pane_idx = i;
  ws->selpane = i;
  if (ISSHOWING(selmon, i)) {
    size_t si = SchemeSel1 + i;
    XSetWindowBorder(dpy, selmon->sel->win, scheme[si][ColBorder].pixel);
  } else {
    unfocus(selmon->sel, 1);
    focus(NULL);
  }

  drawbar(selmon);
  arrange(selmon);
}

void
moveclient_paneidx(const Arg *arg)
{
  if (!selmon->sel) return;
  if (arg->ui >= WS_PANES) return;

  const uint i = arg->ui;
  selmon->sel->pane_idx = i;

  if (ISSHOWING(selmon, i)) {
    size_t si = SchemeSel1 + i;
    XSetWindowBorder(dpy, selmon->sel->win, scheme[si][ColBorder].pixel);
  } else {
    unfocus(selmon->sel, 1);
    focus(NULL);
  }

  drawbar(selmon);
  arrange(selmon);
}

void
moveclient_ws(const Arg *arg)
{
  if (!selmon->sel) return;
  if (arg->ui >= WS_LEN) return;

  selmon->sel->ws_idx = arg->ui;
  unfocus(selmon->sel, 1);
  focus(NULL);

  drawbar(selmon);
  arrange(selmon);
}

void
moveclient(Client *c, int x, int y, int w, int h)
{
  resize(c, x, y, w, h, 0);
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, w / 2, h / 2);
}

void
movemouse(const Arg *arg)
{
  int x, y, ocx, ocy, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		   None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;
  if (!getrootptr(&x, &y))
    return;
  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
	continue;
      lasttime = ev.xmotion.time;

      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);
      if (abs(selmon->wx - nx) < snap)
	nx = selmon->wx;
      else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
	nx = selmon->wx + selmon->ww - WIDTH(c);
      if (abs(selmon->wy - ny) < snap)
	ny = selmon->wy;
      else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
	ny = selmon->wy + selmon->wh - HEIGHT(c);
      if (!c->isfloating
	  && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
	togglefloating(NULL);
      if (c->isfloating)
	resize(c, nx, ny, c->w, c->h, 1);
      break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

void
movestack(const Arg *arg)
{
  if (!selmon->sel || selmon->sel->isfloating) return;

  Client *c = NULL, *p = NULL, *pc = NULL, *i;
  const uint pi = selmon->sel->pane_idx;

  if (arg->i > 0) {
    /* find the client after selmon->sel */
    for (c = selmon->sel->next; c && !ISTILED(c, pi); c = c->next);
    if (!c)
      for (c = selmon->clients; c && !ISTILED(c, pi); c = c->next);
  }
  else {
    /* find the client before selmon->sel */
    for (i = selmon->clients; i != selmon->sel; i = i->next)
      if (ISTILED(i, pi))
	c = i;
    if (!c)
      for (; i; i = i->next)
	if (ISTILED(i, pi))
	  c = i;
  }

  if (!c || c == selmon->sel) return;

  /* find the client before selmon->sel and c */
  for (i = selmon->clients; i && (!p || !pc); i = i->next) {
    if (i->next == selmon->sel)
      p = i;
    if (i->next == c)
      pc = i;
  }

  /* swap c and selmon->sel selmon->clients in the selmon->clients list */
  Client *temp = selmon->sel->next == c ? selmon->sel : selmon->sel->next;
  selmon->sel->next = c->next == selmon->sel ? c : c->next;
  c->next = temp;

  if (p && p != c)
    p->next = c;
  if (pc && pc != selmon->sel)
    pc->next = selmon->sel;

  if (selmon->sel == selmon->clients)
    selmon->clients = c;
  else if (c == selmon->clients)
    selmon->clients = selmon->sel;

  arrange(selmon);
}

Client *
nexttiled(Client *c, uint pi)
{
  if (c) {
    for (; c; c = c->next)
      if (ISTILED(c, pi))
	return c;
  }
  return NULL;
}

void
pop(Client *c)
{
  detach(c);
  attach(c);
  focus(c);
  arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
  Client *c;
  Window trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((ev->window == root) && (ev->atom == XA_WM_NAME))
    drawbars();
  else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window))) {
    switch(ev->atom) {
    default: break;
    case XA_WM_TRANSIENT_FOR:
      if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
	  (c->isfloating = (wintoclient(trans)) != NULL))
	arrange(c->mon);
      break;
    case XA_WM_NORMAL_HINTS:
      c->hintsvalid = 0;
      break;
    case XA_WM_HINTS:
      updatewmhints(c);
      drawbars();
      break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
      updatetitle(c);
      if (c == c->mon->sel && c->mon->bar_info_idx == BAR_INFO_WIN_TITLE)
	drawbar_status(c->mon);
    }
    if (ev->atom == netatom[NetWMWindowType])
      updatewindowtype(c);
  }
}

void
quit(const Arg *arg)
{
  running = 0;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
  Monitor *m, *r = selmon;
  int a, area = 0;

  for (m = mons; m; m = m->next)
    if ((a = INTERSECT(x, y, w, h, m)) > area) {
      area = a;
      r = m;
    }
  return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
  if (applysizehints(c, &x, &y, &w, &h, interact))
    resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
  XWindowChanges wc;

  c->oldx = c->x; c->x = wc.x = x;
  c->oldy = c->y; c->y = wc.y = y;
  c->oldw = c->w; c->w = wc.width = w;
  c->oldh = c->h; c->h = wc.height = h;
  wc.border_width = c->bw;
  XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
  configure(c);
  XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
  int ocx, ocy, nw, nh;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		   None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
	continue;
      lasttime = ev.xmotion.time;

      nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
      nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
      if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
	  && c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
	{
	  if (!c->isfloating
	      && (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
	    togglefloating(NULL);
	}
      if (c->isfloating)
	resize(c, c->x, c->y, nw, nh, 1);
      break;
    }
  } while (ev.type != ButtonRelease);
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
  XUngrabPointer(dpy, CurrentTime);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

void
restack(Monitor *m)
{
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  if (!m->sel)
    return;
  if (m->sel->isfloating)
    XRaiseWindow(dpy, m->sel->win);
  {
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->stack; c; c = c->snext)
      if (!c->isfloating && ISVISIBLE(c)) {
	XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
	wc.sibling = c->win;
      }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
  XEvent ev;
  /* main event loop */
  XSync(dpy, False);
  while (running && !XNextEvent(dpy, &ev))
    if (handler[ev.type])
      handler[ev.type](&ev); /* call handler */
}

void
scan(void)
{
  unsigned int i, num;
  Window d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(dpy, wins[i], &wa)
	  || wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
	continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
	manage(wins[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
	continue;
      if (XGetTransientForHint(dpy, wins[i], &d1)
	  && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
	manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
}

void
sendmon(Client *c, Monitor *m)
{
  if (c->mon == m)
    return;
  unfocus(c, 1);
  detach(c);
  detachstack(c);
  drawbar(c->mon);

  c->mon = m;
  attach(c);
  attachstack(c);
  focus(NULL);
  arrange(NULL);
  drawbar(m);
}

void
setclientstate(Client *c, long state)
{
  long data[] = { state, None };

  XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		  PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *c, Atom proto)
{
  int n;
  Atom *protocols;
  int exists = 0;
  XEvent ev;

  if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
    while (!exists && n--)
      exists = protocols[n] == proto;
    XFree(protocols);
  }
  if (exists) {
    ev.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = wmatom[WMProtocols];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = proto;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, c->win, False, NoEventMask, &ev);
  }
  return exists;
}

void
setbarinfoidx(const Arg *arg)
{
  if (!arg || arg->ui >= BAR_INFO_CNT)
    return;

  selmon->bar_info_idx = arg->ui;
  drawbar_status(selmon);
}

void
setfocus(Client *c)
{
  if (!c->neverfocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow],
		    XA_WINDOW, 32, PropModeReplace,
		    (unsigned char *) &(c->win), 1);
  }
  sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
  if (fullscreen && !c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		    PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
    c->isfullscreen = 1;
    c->oldstate = c->isfloating;
    c->oldbw = c->bw;
    c->bw = 0;
    c->isfloating = 1;
    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    XRaiseWindow(dpy, c->win);
  } else if (!fullscreen && c->isfullscreen){
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		    PropModeReplace, (unsigned char*)0, 0);
    c->isfullscreen = 0;
    c->isfloating = c->oldstate;
    c->bw = c->oldbw;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->mon);
  }
}

void
setup(void)
{
  int i;
  XSetWindowAttributes wa;
  Atom utf8string;
  struct sigaction sa;

  /* do not transform children into zombies when they terminate */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
  sa.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sa, NULL);

  /* clean up any zombies (inherited from .xinitrc etc) immediately */
  while (waitpid(-1, NULL, WNOHANG) > 0);

  /* init screen */
  screen = DefaultScreen(dpy);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  root = RootWindow(dpy, screen);
  drw = drw_create(dpy, screen, root, sw, sh);
  if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
    die("no fonts could be loaded.");
  lrpad = drw->fonts->h;
  bh = drw->fonts->h + 2;
  updategeom();
  /* init atoms */
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
  netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
  cursor[CurResize] = drw_cur_create(drw, XC_sizing);
  cursor[CurMove] = drw_cur_create(drw, XC_fleur);
  /* init appearance */
  scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], 3);
  /* init bars */
  updatebars();
  updatestatus();
  /* supporting window for NetWMCheck */
  wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		  PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		  PropModeReplace, (unsigned char *) WM_MY_NAME, 3);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		  PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  /* EWMH support per view */
  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		  PropModeReplace, (unsigned char *) netatom, NetLast);
  XDeleteProperty(dpy, root, netatom[NetClientList]);
  /* select events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
    |ButtonPressMask|PointerMotionMask|EnterWindowMask
    |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  grabkeys();
  focus(NULL);
}

void
seturgent(Client *c, int urg)
{
  XWMHints *wmh;

  c->isurgent = urg;
  if (urg) c->mon->bar_info_idx = BAR_INFO_WS_OVERVIEW;

  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

void
showhide(Client *c)
{
  if (!c)
    return;
  if (ISVISIBLE(c)) {
    /* show clients top down */
    XMoveWindow(dpy, c->win, c->x, c->y);
    if (c->isfloating && !c->isfullscreen)
      resize(c, c->x, c->y, c->w, c->h, 0);
    showhide(c->snext);
  } else {
    /* hide clients bottom up */
    showhide(c->snext);
    XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
  }
}

void
spawn(const Arg *arg)
{
  Workspace *ws = selmon->selws;
  ws->panes[ws->selpane].showing = 1;

  struct sigaction sa;

  if (arg->v == dmenucmd)
    dmenumon[0] = '0' + selmon->num;
  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);

    execvp(((char **)arg->v)[0], (char **)arg->v);
    die(WM_MY_NAME ": execvp '%s' failed:", ((char **)arg->v)[0]);
  }
}

void
switchworkspace(const Arg *arg)
{
  if (!arg) return;

  int ws_idx, alt_idx;
  if (arg->ws.idx >= 0 && arg->ws.alt >= 0) {
    if (arg->ws.idx >= WS_LEN || arg->ws.alt >= WS_ALTS
	|| (arg->ws.idx == selmon->ws_idx && arg->ws.alt == selmon->alt_idx))
      return;
    ws_idx = arg->ws.idx;
    alt_idx = arg->ws.alt;
  } else {
    ws_idx = selmon->last_ws_idx;
    alt_idx = selmon->last_alt_idx;
  }

  selmon->last_ws_idx = selmon->ws_idx;
  selmon->last_alt_idx = selmon->alt_idx;
  selmon->ws_idx = ws_idx;
  selmon->alt_idx = alt_idx;
  selmon->selws = &selmon->workspaces[ws_idx][alt_idx];

  focus(NULL);
  arrange(selmon);
  drawbar(selmon);
}

void
tile(Monitor *m, uint pi, Rect *r, int vert)
{
  const Pane *p = &m->selws->panes[pi];
  Client *first = nexttiled(m->clients, pi);
  uint n, i;
  Client *c;

  for (n = 0, c = first; c; c = nexttiled(c->next, pi), n++);
  if (!n) return;

  const uint div = p->max_display ? MIN(n, p->max_display) : n;
  const uint lim = div - 1;
  const uint each = div ? (vert ? r->h : r->w) / div : 0;
  const uint rem = vert ? r->h - each * div : r->w - each * div;

  for (i = 0, c = first; n; n--, c = nexttiled(c->next, pi)) {
    c->is_arranged = 1;
    if (vert)
      resize(c, r->x, r->y + MIN(i, lim) * each, r->w - (2*c->bw),
	     each + (i < lim ? 0 : rem) - (2*c->bw), 0);
    else
      resize(c, r->x + MIN(i, lim) * each, r->y,
	     each + (i < lim ? 0 : rem) - (2*c->bw),
	     r->h - (2*c->bw), 0);
    i++;
  }
}

void
tile_h(Monitor *m, uint pi, Rect *r)
{
  tile(m, pi, r, 0);
}

void
tile_v(Monitor *m, uint pi, Rect *r)
{
  tile(m, pi, r, 1);
}

void
togglebar(const Arg *arg)
{
  selmon->showbar = !selmon->showbar;
  updatebarpos(selmon);
  XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
  arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
  if (!selmon->sel)
    return;
  if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
    return;

  selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
  arrange(selmon);
  if (selmon->bar_info_idx == BAR_INFO_WIN_TITLE)
    drawbar_status(selmon);
}

void
togglepane(const Arg *arg)
{
  if (!arg) return;

  const uint pi = arg->ui == 0 ? selmon->selws->selpane : arg->ui - 1;
  if (pi >= WS_PANES) return;

  if (selmon->selws->panes[pi].showing ^= 1) {
    Client *c = selmon->stack;
    for (; c && !(ISCURRENTWS(c) && c->pane_idx == pi); c = c->snext);
    if (c)
      focus(c);
  } else {
    if (selmon->sel && selmon->sel->pane_idx == pi) {
      focus(NULL);
    }
  }

  arrange(selmon);
  drawbar(selmon);
}

void
unfocus(Client *c, int setfocus)
{
  if (!c)
    return;
  grabbuttons(c, 0);
  XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  if (setfocus) {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
}

void
unmanage(Client *c, int destroyed)
{
  Monitor *m = c->mon;
  XWindowChanges wc;

  detach(c);
  detachstack(c);
  if (!destroyed) {
    wc.border_width = c->oldbw;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XSelectInput(dpy, c->win, NoEventMask);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
  free(c);
  focus(NULL);
  updateclientlist();
  arrange(m);
  drawbar(m);
}

void
unmapnotify(XEvent *e)
{
  Client *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = wintoclient(ev->window))) {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, 0);
  }
}

void
updatebars(void)
{
  Monitor *m;
  XSetWindowAttributes wa = {
    .override_redirect = True,
    .background_pixmap = ParentRelative,
    .event_mask = ButtonPressMask|ExposureMask
  };
  XClassHint ch = {WM_MY_NAME, WM_MY_NAME};
  for (m = mons; m; m = m->next) {
    if (m->barwin)
      continue;
    m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
			      CopyFromParent, DefaultVisual(dpy, screen),
			      CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
    XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
    XMapRaised(dpy, m->barwin);
    XSetClassHint(dpy, m->barwin, &ch);
  }
}

void
updatebarpos(Monitor *m)
{
  m->wy = m->my;
  m->wh = m->mh;
  if (m->showbar) {
    m->wh -= bh;
    m->by = m->topbar ? m->wy : m->wy + m->wh;
    m->wy = m->topbar ? m->wy + bh : m->wy;
  } else
    m->by = -bh;
}

void
updateclientlist(void)
{
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      XChangeProperty(dpy, root, netatom[NetClientList],
		      XA_WINDOW, 32, PropModeAppend,
		      (unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
  int dirty = 0;

#ifdef XINERAMA
  if (XineramaIsActive(dpy)) {
    int i, j, n, nn;
    Client *c;
    Monitor *m;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = mons; m; m = m->next, n++);
    /* only consider unique geometries as separate screens */
    unique = ecalloc(nn, sizeof(XineramaScreenInfo));
    for (i = 0, j = 0; i < nn; i++)
      if (isuniquegeom(unique, j, &info[i]))
	memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    XFree(info);
    nn = j;

    /* new monitors if nn > n */
    for (i = n; i < nn; i++) {
      for (m = mons; m && m->next; m = m->next);
      if (m)
	m->next = createmon();
      else
	mons = createmon();
    }
    for (i = 0, m = mons; i < nn && m; m = m->next, i++)
      if (i >= n
	  || unique[i].x_org != m->mx || unique[i].y_org != m->my
	  || unique[i].width != m->mw || unique[i].height != m->mh)
	{
	  dirty = 1;
	  m->num = i;
	  m->mx = m->wx = unique[i].x_org;
	  m->my = m->wy = unique[i].y_org;
	  m->mw = m->ww = unique[i].width;
	  m->mh = m->wh = unique[i].height;
	  updatebarpos(m);
	}
    /* removed monitors if n > nn */
    for (i = nn; i < n; i++) {
      for (m = mons; m && m->next; m = m->next);
      while ((c = m->clients)) {
	dirty = 1;
	m->clients = c->next;
	detachstack(c);
	c->mon = mons;
	attach(c);
	attachstack(c);
      }
      if (m == selmon)
	selmon = mons;
      cleanupmon(m);
    }
    free(unique);
  } else
#endif /* XINERAMA */
    { /* default monitor setup */
      if (!mons)
	mons = createmon();
      if (mons->mw != sw || mons->mh != sh) {
	dirty = 1;
	mons->mw = mons->ww = sw;
	mons->mh = mons->wh = sh;
	updatebarpos(mons);
      }
    }
  if (dirty) {
    selmon = mons;
    selmon = wintomon(root);
  }
  return dirty;
}

void
updatenumlockmask(void)
{
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(dpy);
  for (i = 0; i < 8; i++)
    for (j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[i * modmap->max_keypermod + j]
	  == XKeysymToKeycode(dpy, XK_Num_Lock))
	numlockmask = (1 << i);
  XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  if (size.flags & PBaseSize) {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  } else if (size.flags & PMinSize) {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  } else
    c->basew = c->baseh = 0;
  if (size.flags & PResizeInc) {
    c->incw = size.width_inc;
    c->inch = size.height_inc;
  } else
    c->incw = c->inch = 0;
  if (size.flags & PMaxSize) {
    c->maxw = size.max_width;
    c->maxh = size.max_height;
  } else
    c->maxw = c->maxh = 0;
  if (size.flags & PMinSize) {
    c->minw = size.min_width;
    c->minh = size.min_height;
  } else if (size.flags & PBaseSize) {
    c->minw = size.base_width;
    c->minh = size.base_height;
  } else
    c->minw = c->minh = 0;
  if (size.flags & PAspect) {
    c->mina = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
  } else
    c->maxa = c->mina = 0.0;
  c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
  c->hintsvalid = 1;
}

void
updatestatus(void)
{
  for (Monitor *m = mons; m; m = m->next)
    if (m->bar_info_idx >= BAR_INFO_CUSTOM)
      drawbar_status(m);
}

void
updatetitle(Client *c)
{
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

void
updatewindowtype(Client *c)
{
  Atom state = getatomprop(c, netatom[NetWMState]);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  if (state == netatom[NetWMFullscreen])
    setfullscreen(c, 1);
  if (wtype == netatom[NetWMWindowTypeDialog])
    c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win))) {
    if (c == selmon->sel && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    } else {
      c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
      if (c->isurgent) c->mon->bar_info_idx = BAR_INFO_WS_OVERVIEW;
    }
    if (wmh->flags & InputHint)
      c->neverfocus = !wmh->input;
    else
      c->neverfocus = 0;
    XFree(wmh);
  }
}

Client *
wintoclient(Window w)
{
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      if (c->win == w)
	return c;
  return NULL;
}

Monitor *
wintomon(Window w)
{
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);
  for (m = mons; m; m = m->next)
    if (w == m->barwin)
      return m;
  if ((c = wintoclient(w)))
    return c->mon;
  return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
  if (ee->error_code == BadWindow
      || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
      || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
      || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
      || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
      || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
      || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
      || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
      || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    return 0;
  fprintf(stderr, WM_MY_NAME ": fatal error: request code=%d, error code=%d\n",
	  ee->request_code, ee->error_code);
  return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
  return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
  die(WM_MY_NAME ": another window manager is already running");
  return -1;
}

void
zoom(const Arg *arg)
{
  Client *c = selmon->sel;
  if (!c || c->isfloating) return;

  const uint pi = c->pane_idx;
  if (c == nexttiled(selmon->clients, pi) && !(c = nexttiled(c->next, pi)))
    return;
  pop(c);
}

int timer_looping;

void*
timer_loop(void* v)
{
  while (timer_looping) {
    XStoreName(dpy, root, "");
    XFlush(dpy);

    sleep(1);
  }
  return NULL;
}

int
main(int argc, char *argv[])
{
  if (argc == 2 && !strcmp("-v", argv[1]))
    die(WM_MY_NAME "-" VERSION);
  else if (argc != 1)
    die("usage: " WM_MY_NAME " [-v]");
  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);
  if (!(dpy = XOpenDisplay(NULL)))
    die(WM_MY_NAME ": cannot open display");
  checkotherwm();
  setup();
#ifdef __OpenBSD__
  if (pledge("stdio rpath proc exec", NULL) == -1)
    die("pledge");
#endif /* __OpenBSD__ */
  scan();

  timer_looping = 1;
  pthread_t timer;
  pthread_create(&timer, NULL, timer_loop, NULL);

  run();

  timer_looping = 0;
  pthread_join(timer, NULL);

  cleanup();
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
