/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <locale.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xft/Xft.h>

static Atom net_supported;
static Atom net_client_list;
static Atom net_number_of_desktops;
static Atom net_current_desktop;
static Atom net_desktop_names;
static Atom net_active_window;
static Atom net_wm_strut_partial;
static Atom net_wm_name;
static Atom net_wm_window_type;
static Atom net_wm_window_type_dock;
static Atom echinus_seltags;
static Atom wm_name;
static Atom wm_delete;
static Atom wm_proto;
static Atom net_window_desktop;
static Atom utf8_string;
typedef struct
{
    const char *name;
    Atom *atom;
} AtomItem;

static AtomItem AtomNames[] =
{
    { "_NET_SUPPORTED", &net_supported },
    { "_NET_CLIENT_LIST", &net_client_list },
    { "_NET_NUMBER_OF_DESKTOPS", &net_number_of_desktops },
    { "_NET_CURRENT_DESKTOP", &net_current_desktop },
    { "_NET_DESKTOP_NAMES", &net_desktop_names },
    { "_NET_ACTIVE_WINDOW", &net_active_window },
    { "_NET_WM_NAME", &net_wm_name },
    { "_NET_WM_DESKTOP", &net_window_desktop },
    { "_NET_WM_STRUT_PARTIAL", &net_wm_strut_partial },
    { "_NET_WM_WINDOW_TYPE", &net_wm_window_type },
    { "_NET_WM_WINDOW_TYPE_DOCK", &net_wm_window_type_dock },
    { "_ECHINUS_SELTAGS", &echinus_seltags },
    { "WM_NAME", &wm_name },
    { "WM_DELETE_WINDOW", &wm_delete },
    { "WM_PROTOCOLS", &wm_proto },
    { "UTF8_STRING", &utf8_string },
};

#define NATOMS (sizeof(AtomNames)/sizeof(AtomItem)) 

/* macros */
#define CLEANMASK(mask) (mask & ~(numlockmask | LockMask))
#define RESNAME                        "ourico"
#define RESCLASS               "Ourico"

/* enums */
enum { ColBG, ColBorder, ColLast };

/* typedefs */
typedef struct {
	int x, y, w, h;
	unsigned long norm[ColLast];
	unsigned long sel[ColLast];
	unsigned long desk[ColLast];
        XftColor *xftnorm;
        XftColor *xftsel;
        XftColor *xftdesk;
	Drawable drawable;
        XftDraw *xftdrawable;
	GC gc;
	struct {
            XftFont *xftfont;
            XGlyphInfo *extents;
            int height;
            int width;
	} font;
} DC; /* draw context */

/* forward declarations */
char* estrdup(const char *str);
int xerrordummy(Display *dpy, XErrorEvent *ee);
void initatoms(void);
void cleanup(void);
void drawtext(const char *text, unsigned long col[ColLast], Bool center, Bool border);
void* emalloc(unsigned int size);
void eprint(const char *errstr, ...);
unsigned long getcolor(const char *colstr);
char *getresource(const char *resource, char *defval);
void initfont(const char *fontstr);
void setup(Bool bottom);
void* getatom(Window w, Atom atom, unsigned long *n);
char** getutf8prop(Window win, Atom atom, int *count);
Bool gettextprop(Window w, Atom atom, char *text, unsigned int size);
void updateatoms();
void drawme();
void focus(Window w);
void buttonpress(XButtonEvent ev);
void run(void);
void setstruts(Bool bottom);
unsigned int textnw(const char *text, unsigned int len);
unsigned int textw(const char *text);

#include "config.h"

/* variables */
char *font = FONT;
char *maxname = NULL;
char *normbg = NORMBGCOLOR;
char *normfg = NORMFGCOLOR;
char *deskbg = NORMBGCOLOR;
char *deskfg = NORMFGCOLOR;
char *normborder = NORMFGCOLOR;
char *selborder = SELFGCOLOR;
char *deskborder = NORMFGCOLOR;
char *selbg = SELBGCOLOR;
char *selfg = SELFGCOLOR;
char *clockformat = NULL;
int screen;
int ret = 0;
int border = 1;
int topbottom = 0;
unsigned int mw = 0;
unsigned int mh = 0;
unsigned int mx = 0;
unsigned int numlockmask = 0;
Bool showclock = False;
Bool running = True;
Bool showdesk = True;
Bool showtagbar = False;
Bool focusonly = False;
int raise = False;
Display *dpy;
DC dc = {0};
Window root, win;
XrmDatabase xrdb = NULL;
int *ndesk, *curdesk;
int ndesknames;
Bool *seltags;
Window *w;
Window *list;
unsigned long nclients = 0;
unsigned long onclients = 0;
char **deskname = NULL;


char *
estrdup(const char *str) {
	char *res = strdup(str);

	if(!res)
		eprint("fatal: could not malloc() %u bytes\n", strlen(str));
	return res;
}

int
xerrordummy(Display *dpy, XErrorEvent *ee) {
    return 0;
}

void
initatoms(void) {
    unsigned int i;
    char *names[NATOMS];
    Atom atoms[NATOMS];
    
    for(i = 0; i < NATOMS; i++)
        names[i] = (char *) AtomNames[i].name;
    int s = XInternAtoms(dpy, names, NATOMS, False, atoms);
    if( s == BadAlloc || s == BadAtom || s == BadValue)
        puts("life sucks");
    for(i = 0; i < NATOMS; i++)
        *AtomNames[i].atom = atoms[i];
}

void
cleanup(void) {
	XFreePixmap(dpy, dc.drawable);
	XFreeGC(dpy, dc.gc);
	XDestroyWindow(dpy, win);
	XUngrabKeyboard(dpy, CurrentTime);
}

void
drawtext(const char *text, unsigned long col[ColLast], Bool center, Bool border) {
	int x, y, w, h;
	static char buf[256];
	unsigned int len, olen;
	XRectangle r = { dc.x, 0, dc.w, dc.h };
	XSetForeground(dpy, dc.gc, col[ColBG]);
	XSetBackground(dpy, dc.gc, col[ColBG]);
	XFillRectangles(dpy, dc.drawable, dc.gc, &r, 1);
	if(!text)
		return;
	w = 0;
	olen = len = strlen(text);
	if(len >= sizeof buf)
		len = sizeof buf - 1;
	memcpy(buf, text, len);
	buf[len] = 0;
	h = dc.h;
	y = dc.h-dc.font.height/2 - 1;
	x = dc.x+dc.font.height/2;
        /* shorten text if necessary */
	while(len && (w = textnw(buf, len)) > dc.w)
		buf[--len] = 0;
	if(len < olen) {
		if(len > 1)
			buf[len - 1] = '.';
		if(len > 2)
			buf[len - 2] = '.';
		if(len > 3)
			buf[len - 3] = '.';
	}
	if(w > dc.w)
		return; /* too long */
        if(center)
                x = dc.x + dc.w/2 - w/2;
        while(x <= 0)
                x = dc.x++;
        XftColor *c;
        if(col == dc.norm)
            c = dc.xftnorm;
        else if (col == dc.sel)
            c = dc.xftsel;
        else 
            c = dc.xftdesk;
        XftDrawStringUtf8(dc.xftdrawable, c,
                dc.font.xftfont, x, y, (unsigned char*)buf, len);
        if(border){
            XSetForeground(dpy, dc.gc, col[ColBorder]);
            XDrawRectangle(dpy, dc.drawable, dc.gc, dc.x, dc.y, dc.w, dc.h-1);
        }
        if(topbottom){
            XSetForeground(dpy, dc.gc, col[ColBorder]);
            XDrawLine(dpy, dc.drawable, dc.gc, 0, 0, dc.w, 0);
            XDrawLine(dpy, dc.drawable, dc.gc, 0, dc.h, dc.w, dc.h);
        }
}

void *
emalloc(unsigned int size) {
	void *res = malloc(size);

	if(!res)
		eprint("fatal: could not malloc() %u bytes\n", size);
	return res;
}

void *
emallocz(unsigned int size) {
    void *res = calloc(1, size);

    if(!res)
            eprint("fatal: could not malloc() %u bytes\n", size);
    return res;
}

void
eprint(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

unsigned long
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor color;

	if(!XAllocNamedColor(dpy, cmap, colstr, &color, &color))
		eprint("error, cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

char *
getresource(const char *resource, char *defval) {
   static char name[256], class[256], *type;
   XrmValue value;
   snprintf(name, sizeof(name), "%s.%s", RESNAME, resource);
   snprintf(class, sizeof(class), "%s.%s", RESCLASS, resource);
   XrmGetResource(xrdb, name, class, &type, &value);
   if(value.addr)
           return value.addr;
   return defval;
}

void
initfont(const char *fontstr) {
    dc.font.xftfont = XftFontOpenXlfd(dpy,screen,fontstr);
    if(!dc.font.xftfont)
         dc.font.xftfont = XftFontOpenName(dpy,screen,fontstr);
    if(!dc.font.xftfont)
         eprint("error, cannot load font: '%s'\n", fontstr);
    dc.font.extents = emalloc(sizeof(XGlyphInfo));
    XftTextExtentsUtf8(dpy, dc.font.xftfont,(unsigned char*)fontstr, strlen(fontstr), dc.font.extents);
    dc.font.height = dc.font.extents->y+2*dc.font.extents->yOff;
    dc.font.width = dc.font.extents->xOff;
}

void
setup(Bool bottom) {
	unsigned int i, j;
	XModifierKeymap *modmap;
	XSetWindowAttributes wa;

	/* init modifier map */
	modmap = XGetModifierMapping(dpy);
	for(i = 0; i < 8; i++)
		for(j = 0; j < modmap->max_keypermod; j++) {
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
		}
	XFreeModifiermap(modmap);
        /* init resources */
        XrmInitialize();
        chdir(getenv("HOME"));
        xrdb = XrmGetFileDatabase(".ouricorc");
        if(!xrdb)
            eprint("echinus: cannot open configuration file\n");

	/* style */
        dc.norm[ColBorder] = getcolor(getresource("normal.border",NORMBORDERCOLOR));
	dc.norm[ColBG] = getcolor(getresource("normal.bg",NORMBGCOLOR));

        dc.sel[ColBorder] = getcolor(getresource("selected.border", SELBORDERCOLOR));
	dc.sel[ColBG] = getcolor(getresource("selected.bg", SELBGCOLOR));

        dc.desk[ColBorder] = getcolor(getresource("deskname.border", SELBORDERCOLOR));
	dc.desk[ColBG] = getcolor(getresource("deskname.bg", SELBGCOLOR));

        dc.xftsel = emalloc(sizeof(XftColor));
        dc.xftnorm = emalloc(sizeof(XftColor));
        dc.xftdesk = emalloc(sizeof(XftColor));
        XftColorAllocName(dpy, DefaultVisual(dpy,screen), DefaultColormap(dpy,screen), getresource("selected.fg", SELFGCOLOR), dc.xftsel);
        XftColorAllocName(dpy, DefaultVisual(dpy,screen), DefaultColormap(dpy,screen), getresource("normal.fg", NORMFGCOLOR), dc.xftnorm);
        XftColorAllocName(dpy, DefaultVisual(dpy,screen), DefaultColormap(dpy,screen), getresource("deskname.fg", SELFGCOLOR), dc.xftdesk);
        if(!dc.xftnorm || !dc.xftnorm || !dc.xftdesk)
            eprint("error, cannot allocate color\n");
        initfont(getresource("font", FONT));
        mw = atoi(getresource("width", "0"));
        mh = atoi(getresource("height", "13"));
        border = atoi(getresource("borderpx", "1"));
        showdesk = atoi(getresource("showdesktop", "1"));
        showclock = atoi(getresource("showclock", "1"));
        showtagbar = atoi(getresource("showtagbar", "0"));
        focusonly = atoi(getresource("focusonly", "0"));
        bottom = atoi(getresource("bottom", "0"));
        raise = atoi(getresource("raise", "0"));
        mx = atoi(getresource("x", "0"));
        topbottom = atoi(getresource("tblines", "0"));
	clockformat = getresource("clockformat", "%H:%M:%S");

	/* menu window */
	wa.override_redirect = 0;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask;
        if(!mw)
            mw = DisplayWidth(dpy, screen)-mx;
        if(!mh)
            mh = dc.font.height;
	win = XCreateWindow(dpy, root, mx,
			bottom ? DisplayHeight(dpy, screen) - mh : 0, mw, mh, 0,
			DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen),
			CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

	/* pixmap */
	dc.drawable = XCreatePixmap(dpy, root, mw, mh, DefaultDepth(dpy, screen));
	dc.gc = XCreateGC(dpy, root, 0, 0);
	XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);

        dc.xftdrawable = XftDrawCreate(dpy, dc.drawable, DefaultVisual(dpy,screen), DefaultColormap(dpy,screen));
        if(!dc.xftdrawable)
            eprint("error, cannot create drawable\n");
        initatoms();
        setstruts(bottom);
	XMapRaised(dpy, win);
}

void*
getatom(Window w, Atom atom, unsigned long *n) {
	int format, status;
	unsigned char *p = NULL;
	unsigned long tn, extra;
	Atom real;

	status = XGetWindowProperty(dpy, w, atom, 0L, 64L, False, AnyPropertyType,
			&real, &format, &tn, &extra, (unsigned char **)&p);
	if(status == BadWindow)
		return NULL;
        if(n!=NULL)
            *n = tn;

	return p;
}

char **
getutf8prop(Window win, Atom atom, int *count) {
    Atom type;
    int format, i;
    unsigned long nitems;
    unsigned long bytes_after;
    char *s, **retval = NULL;
    int result;
    unsigned char *tmp = NULL;

    *count = 0;
    result = XGetWindowProperty(dpy, win, atom, 0, 64L, False,
          utf8_string, &type, &format, &nitems,
          &bytes_after, &tmp);
    if (result != Success || type != utf8_string || tmp == NULL)
        return NULL;

    if (nitems) {
        char *val = (char *) tmp;
        for (i = 0; i < nitems; i++) {
            if (!val[i])
                (*count)++;
        }
        retval = emalloc((*count + 2)*sizeof(char*));
        for (i = 0, s = val; i < *count; i++, s = s +  strlen (s) + 1) {
            retval[i] = estrdup(s);
        }
        if (val[nitems-1]) {
            result = nitems - (s - val);
            memmove(s - 1, s, result);
            val[nitems-1] = 0;
            retval[i] = estrdup(s - 1);
            (*count)++;
        }
    }
    XFree (tmp);

    return retval;

}

Bool
gettextprop(Window w, Atom atom, char *text, unsigned int size) {
	char **list = NULL;
	int n;
	XTextProperty name;

	if(!text || size == 0)
		return False;
	text[0] = '\0';
	if (BadWindow == XGetTextProperty(dpy, w, &name, atom))
            return False;
	if(!name.nitems)
		return False;
	if(name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if(XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success
		&& n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return True;
}

void
updateatoms(){
    int i;
    if(ndesk)
        XFree(ndesk);
    if(seltags)
        XFree(seltags);
    if(curdesk)
        XFree(curdesk);
    if(w)
        XFree(w);
    if(list)
        XFree(list);
    if(deskname){
        for(i = 0; i<ndesknames; i++){
            free(deskname[i]);
        }
    }
    ndesk = getatom(root, net_number_of_desktops, NULL);
    seltags = getatom(root, echinus_seltags, NULL);
    curdesk = getatom(root, net_current_desktop, NULL);
    w = getatom(root, net_active_window, NULL);
    list = getatom(root, net_client_list, &nclients);
    deskname = getutf8prop(root, net_desktop_names, &ndesknames); 
}

int
drawclock() {
	time_t t;
	time(&t);
	struct tm *tp = localtime(&t);
	char s[20];
	if(!showclock)
	    return 0;
	bzero(s, 20);
	strftime(s, 20, clockformat, tp);
	dc.w = textw(s);
	dc.x = mw - dc.w;
	drawtext(s, dc.desk, False, border);
	XCopyArea(dpy, dc.drawable, win, dc.gc, dc.x, dc.y, mw, mh, dc.x, dc.y);
	return dc.w;
}

void
drawme(){
    int ww = mw;
    dc.x = 0;
    dc.y = 0;
    dc.w = mw;
    dc.h = mh;

    int i,n;
    n = 0;

    char title[255];
    int *wcd;
    int wn = 0;
    drawtext(NULL, dc.norm, False, False);
    ww = mw - drawclock();
    dc.x = 0;
    for(i = 0; i < nclients && list[i]; i++){
        if(list[i] == win)
            continue;
        wcd = getatom(list[i], net_window_desktop, NULL);
        if(!wcd)
            return;
        if(*wcd == *curdesk)
            wn++;
        XFree(wcd);
	XSelectInput(dpy, list[i], PropertyChangeMask);
    }
    if(showtagbar && seltags){
        for(i = 0; i < ndesknames; i++){
            dc.w = textw(deskname[i]);
            if(seltags[i*2])
	    {
		//printf("%i",i);
                drawtext(deskname[i], dc.sel, False, border);
		
	    }
            else
	    {
                drawtext(deskname[i], dc.norm, False, border);
	    }
            dc.x += dc.w;
        }
    }
    else
        if(showdesk) {
            dc.w = textw(deskname[*curdesk]);
            drawtext(deskname[*curdesk], dc.desk, False, border);
            dc.x += dc.w;
        }
    if(wn)
        dc.w = (ww-dc.x)/wn;
    else
        nclients = 0;
    if(focusonly && wn)
        wn = 1;
    for(i = 0; i < nclients && list[i] && w; i++){
        wcd = getatom(list[i], net_window_desktop, NULL);
        if(list[i] == win)
            continue;
        if(!wcd)
            return;
        if(*wcd != *curdesk){
            XFree(wcd);
            continue;
        }
        XFree(wcd);
        if(!gettextprop(list[i], net_wm_name, title, sizeof title))
            gettextprop(list[i], wm_name, title, sizeof title);
        if(n==wn-1){
            dc.w = ww - dc.x - 1;
        }
        if(list[i] == *w ){
            drawtext(title, dc.sel, True, border);
            if(focusonly)
                break;
        }
        else
            drawtext(title, dc.norm, True, border);
        dc.x += dc.w+2;
        n++;
    }
    XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, ww, mh, 0, 0);
    if(raise)
        XRaiseWindow(dpy, win);
    XFlush(dpy);
}

Bool
isprotodel(Window w) {
	int i, n;
	Atom *protocols;
	Bool ret = False;

	if(XGetWMProtocols(dpy, w, &protocols, &n)) {
		for(i = 0; !ret && i < n; i++)
			if(protocols[i] == wm_delete)
				ret = True;
		XFree(protocols);
	}
	return ret;
}

void
killclient(Window w) {
    XEvent ev;
	if(isprotodel(w)) {
		ev.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = wm_proto;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = wm_delete;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, w, False, NoEventMask, &ev);
	}
	else
		XKillClient(dpy, w);
}

void
setstruts(Bool bottom) {
    int *struts;
    struts = emallocz(12*sizeof(int));
    if(bottom){
        struts[3] = mh;
        struts[10] = mx;
        struts[11] = mx+mw;
    }
    else {
        struts[2] = mh;
        struts[8] = mx;
        struts[9] = mx+mw;
    }
    XChangeProperty(dpy, win, net_wm_strut_partial, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)struts, 12);
    XChangeProperty(dpy, win, net_wm_window_type, XA_ATOM, 32, PropModeReplace, (unsigned char*)&net_wm_window_type_dock, 1);
    free(struts);
}

void 
switchdesktop(int n){
    XEvent ev;
    ev.type = ClientMessage;
    ev.xclient.message_type = net_current_desktop;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = n;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, root, False, (SubstructureNotifyMask|SubstructureRedirectMask), &ev);
}

void 
focus(Window w){
    XEvent ev;
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = net_active_window;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 0;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, root, False, (SubstructureNotifyMask|SubstructureRedirectMask), &ev);
}

void 
buttonpress(XButtonEvent ev){
    int i, *wcd, x, wn;
    wcd = 0;
    wn = 0;
    x = 0;
    if(showtagbar && seltags){
        for(i = 0; i < ndesknames ; i++){
            x += textw(deskname[i]);
            if(ev.x < x){
                    switchdesktop(i);
                    return;
            }
        }
    }
    else
    if(showdesk){
        x += textw(deskname[*curdesk]);
        if(ev.x < x){
            if(ev.button == Button1 && *curdesk < *ndesk-1)
                switchdesktop(*curdesk+1);
            if(ev.button == Button3 && *curdesk)
                switchdesktop(*curdesk-1);
            return;
        }
    }
    for(i = 0; i < nclients && list[i]; i++){
        if(list[i] == win)
            continue;
        wcd = getatom(list[i], net_window_desktop, NULL);
        if(!wcd)
            return;
        if(*wcd == *curdesk)
            wn++;
    }
    for(i = 0; i < nclients && list[i] && w; i++){
        if(list[i] == win)
            continue;
        wcd = getatom(list[i], net_window_desktop, NULL);
        if(*wcd != *curdesk)
            continue;
        x += mw/wn;
        if(ev.x < x){
            if(ev.button == Button1)
                focus(list[i]);
            if(ev.button == Button3)
                killclient(list[i]);
            return;
        }
    }

}

void
run(void) {
	XEvent ev;
	fd_set rd;
	int xfd;
	struct timeval tv;

	xfd = ConnectionNumber(dpy);
        updateatoms();
	XSync(dpy, False);
        XSetErrorHandler(xerrordummy);
	XSync(dpy, False);
	
	XSelectInput(dpy, root, PropertyChangeMask);
	while(running) {
		FD_ZERO(&rd);
		FD_SET(xfd, &rd);

		tv.tv_sec = 0;
		tv.tv_usec = 100;

		while(XPending(dpy)) {
			XNextEvent(dpy, &ev);
			switch (ev.type) {
			default:	/* ignore all crap */
				break;
			case Expose:
				if(ev.xexpose.count == 0)
					drawme();
				break;
			case PropertyNotify:
				updateatoms();
				drawme();
				break;
			case ButtonPress:
				buttonpress(ev.xbutton);
				break;
			}
		}
		XFlush(dpy);
                if(select(0, NULL, NULL, NULL, &tv) == -1) {
                        if(errno == EINTR)
                                continue;
                        eprint("select failed\n");
                }
		drawclock();
	}
}

unsigned int
textnw(const char *text, unsigned int len) {
    XftTextExtentsUtf8(dpy, dc.font.xftfont, (unsigned char*)text, strlen(text), dc.font.extents);
    return dc.font.extents->xOff;
}

unsigned int
textw(const char *text) {
	if(!text)
	    return 0;
	return textnw(text, strlen(text))+mh/2;
}

int
main(int argc, char *argv[]) {
	Bool bottom = False;
	unsigned int i;

	/* command line args */
	for(i = 1; i < argc; i++)
		if(!strcmp(argv[i], "-v"))
			eprint("ourico-"VERSION", © 2008 Alexander Polakov\n");
		else
			eprint("usage: ourico [-v]\n");
	setlocale(LC_CTYPE, "");
	dpy = XOpenDisplay(0);
	if(!dpy)
		eprint("ourico: cannot open display\n");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	setup(bottom);
	XSync(dpy, False);
	run();
	cleanup();
	XCloseDisplay(dpy);
	return ret;
}
