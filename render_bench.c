#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrender.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define REPS 4096

typedef struct _xrender_surf
{
   int       w, h;
   int       depth;
   Visual   *vis;
   Drawable  draw;
   Picture   pic;
   int       allocated : 1;
} Xrender_Surf;

double get_time(void);
void   time_test(char *description, void (*func) (void));

Xrender_Surf *xrender_surf_new(Display *disp, Drawable draw, Visual *vis, int w, int h, int alpha);
Xrender_Surf *xrender_surf_adopt(Display *disp, Drawable draw, Visual *vis, int w, int h);
void          xrender_surf_free(Display *disp, Xrender_Surf *rs);
void          xrender_surf_populate(Display *disp, Xrender_Surf *rs,
				    int w, int h, int stride, int channels,
				    guchar *img_data);
void          xrender_surf_blend(Display *disp, Xrender_Surf *src, Xrender_Surf *dst, int x, int y, int w, int h, int smooth);

void populate_from_file(Display *disp, Xrender_Surf *rs, char *file);
void main_loop(void);
void setup_window(void);

int               win_w = 320;
int               win_h = 320;
static Display   *disp = NULL;
static Window     win;

double
get_time(void)
{
   struct timeval      timev;
   
   gettimeofday(&timev, NULL);
   return (double)timev.tv_sec + (((double)timev.tv_usec) / 1000000);
}

void
time_test(char *description, void (*func) (void))
{
   double t1, t2, t;
   int i;

   printf("---------------------------------------------------------------\n");
   printf("Test: %s\n", description);
   t1 = get_time();
   for (i = 0; i < REPS; i++) func();
   XSync(disp, False);
   t2 = get_time();
   t = t2 - t1;
   printf("Time: %3.3f sec.\n", t);
}

Xrender_Surf *
xrender_surf_new(Display *disp, Drawable draw, Visual *vis, int w, int h, int alpha)
{
   Xrender_Surf *rs;
   XRenderPictFormat *fmt;
   XRenderPictureAttributes att;
   
   rs = calloc(1, sizeof(Xrender_Surf));
   if (alpha)
     fmt = XRenderFindStandardFormat(disp, PictStandardARGB32);
   else
     fmt = XRenderFindStandardFormat(disp, PictStandardRGB24);
   rs->w = w;
   rs->h = h;
   rs->depth = fmt->depth;
   rs->vis = vis;
   rs->draw = XCreatePixmap(disp, draw, w, h, fmt->depth);
   att.dither = 1;
   att.component_alpha = 1;
   att.repeat = 0;
   rs->pic = XRenderCreatePicture(disp, rs->draw, fmt, CPRepeat | CPDither | CPComponentAlpha, &att);
   rs->allocated = 1;
   return rs;
}

Xrender_Surf *
xrender_surf_adopt(Display *disp, Drawable draw, Visual *vis, int w, int h)
{
   Xrender_Surf *rs;
   XRenderPictFormat *fmt;
   XRenderPictureAttributes att;
   
   rs = calloc(1, sizeof(Xrender_Surf));
   fmt = XRenderFindVisualFormat(disp, vis);
   rs->w = w;
   rs->h = h;
   rs->depth = fmt->depth;
   rs->vis = vis;
   rs->draw = draw;
   att.dither = 1;
   att.component_alpha = 1;
   att.repeat = 0;
   rs->pic = XRenderCreatePicture(disp, rs->draw, fmt, CPRepeat | CPDither | CPComponentAlpha, &att);
   rs->allocated = 0;
   return rs;
}

void
xrender_surf_free(Display *disp, Xrender_Surf *rs)
{
   if (rs->allocated) XFreePixmap(disp, rs->draw);
   XRenderFreePicture(disp, rs->pic);
   free(rs);
}

void
xrender_surf_populate(Display *disp, Xrender_Surf *rs,
		      int w, int h, int stride, int channels,
		      guchar *img_data)
{
   GC gc;
   XGCValues gcv;
   XImage *xim;
   int x, y;
   
   /* yes this isn't optimal - i know.. i just want some data for now */
   gc = XCreateGC(disp, rs->draw, 0, &gcv);
   xim = XCreateImage(disp, rs->vis, rs->depth, ZPixmap, 0, NULL, w, h, 32, 0);
   xim->data = malloc(xim->bytes_per_line * xim->height);

   for (y = 0; y < h; y++)
     {
	for (x = 0; x < w; x++)
	  {
	     int pixel;
	     int a, r, g, b;
	     guchar * p = img_data + y * stride + x * channels;
	     r = p[0];
	     g = p[1];
	     b = p[2];
	     a = p[3];
	     XPutPixel(xim, x, y, (a << 24) | (r << 16) | (g << 8) | b);
	  }
     }
   XPutImage(disp, rs->draw, gc, xim, 0, 0, 0, 0, w, h);
   free(xim->data);
   xim->data = NULL;
   XDestroyImage(xim);
   XFreeGC(disp, gc);
}

void
xrender_surf_blend(Display *disp, Xrender_Surf *src, Xrender_Surf *dst, int x, int y, int w, int h, int smooth)
{
   XFilters *flt;
   XTransform xf;
   
   xf.matrix[0][0] = (65536 * src->w) / w; xf.matrix[0][1] = 0; xf.matrix[0][2] = 0;
   xf.matrix[1][0] = 0; xf.matrix[1][1] = (65536 * src->h) / h; xf.matrix[1][2] = 0;
   xf.matrix[2][0] = 0; xf.matrix[2][1] = 0; xf.matrix[2][2] = 65536;
   if (smooth) XRenderSetPictureFilter(disp, src->pic, "bilinear", NULL, 0);
   else XRenderSetPictureFilter(disp, src->pic, "nearest", NULL, 0);
   XRenderSetPictureTransform(disp, src->pic, &xf);
   XRenderComposite(disp, PictOpOver, src->pic, None, dst->pic, 0, 0, 0, 0, x, y, w, h);
}

void
populate_from_file(Display *disp, Xrender_Surf *rs, char *file)
{
   guchar *pixels;
   int w, h, stride, channels;
   GdkPixbuf * pixbuf = NULL;
   GError    * error = NULL;
   
   pixbuf = gdk_pixbuf_new_from_file (file, &error);

   if (error)
   {
       g_critical ("failed loading file [%s], '%s'", file, error->message);
       g_error_free (error);
       exit(1);
   }

   if (!pixbuf)
   {
       g_critical ("failed loading file [%s], perhaps OOM ?", file);
       exit(1);
   }
   
   pixels   = gdk_pixbuf_get_pixels (pixbuf);
   channels = gdk_pixbuf_get_n_channels (pixbuf);
   w        = gdk_pixbuf_get_width (pixbuf);
   h        = gdk_pixbuf_get_height (pixbuf);
   stride   = gdk_pixbuf_get_rowstride (pixbuf);
   
   xrender_surf_populate(disp, rs, w, h, stride, channels, pixels);

   g_object_unref (G_OBJECT(pixbuf));
}

Xrender_Surf *surf_win = NULL;
Xrender_Surf *surf_off = NULL;
Xrender_Surf *surf_img = NULL;

void
test_over_x(void)
{
   int x, y;
   
   x = rand() % (surf_win->w - surf_img->w);
   y = rand() % (surf_win->h - surf_img->h);
   xrender_surf_blend(disp, surf_img, surf_win, x, y, surf_img->w, surf_img->h, 1);
}

void
test_over_off_x(void)
{
   int x, y;
   
   x = rand() % (surf_off->w - surf_img->w);
   y = rand() % (surf_off->h - surf_img->h);
   xrender_surf_blend(disp, surf_img, surf_off, x, y, surf_img->w, surf_img->h, 1);
}


void
test_over_scale_half_x(void)
{
   int x, y;
   
   x = rand() % (surf_win->w - (surf_img->w / 2));
   y = rand() % (surf_win->h - (surf_img->h / 2));
   xrender_surf_blend(disp, surf_img, surf_win, x, y, surf_img->w / 2, surf_img->h / 2, 0);
}

void
test_over_off_scale_half_x(void)
{
   int x, y;
   
   x = rand() % (surf_off->w - (surf_img->w / 2));
   y = rand() % (surf_off->h - (surf_img->h / 2));
   xrender_surf_blend(disp, surf_img, surf_off, x, y, surf_img->w / 2, surf_img->h / 2, 0);
}

void
test_over_scale_double_smooth_x(void)
{
   int x, y;
   
   x = rand() % (surf_win->w - (surf_img->w * 2));
   y = rand() % (surf_win->h - (surf_img->h * 2));
   xrender_surf_blend(disp, surf_img, surf_win, x, y, surf_img->w * 2, surf_img->h * 2, 1);
}

void
test_over_off_scale_double_smooth_x(void)
{
   int x, y;
   
   x = rand() % (surf_off->w - (surf_img->w * 2));
   y = rand() % (surf_off->h - (surf_img->h * 2));
   xrender_surf_blend(disp, surf_img, surf_off, x, y, surf_img->w * 2, surf_img->h * 2, 1);
}

void
test_over_scale_double_nearest_x(void)
{
   int x, y;
   
   x = rand() % (surf_win->w - (surf_img->w * 2));
   y = rand() % (surf_win->h - (surf_img->h * 2));
   xrender_surf_blend(disp, surf_img, surf_win, x, y, surf_img->w * 2, surf_img->h * 2, 0);
}

void
test_over_off_scale_double_nearest_x(void)
{
   int x, y;
   
   x = rand() % (surf_off->w - (surf_img->w * 2));
   y = rand() % (surf_off->h - (surf_img->h * 2));
   xrender_surf_blend(disp, surf_img, surf_off, x, y, surf_img->w * 2, surf_img->h * 2, 0);
}

int count = 0;

void
test_over_scale_general_nearest_x(void)
{
   int w, h;
   
   w = 1 + ((surf_img->w * count) / (REPS / 16));
   h = 1 + ((surf_img->h * count) / (REPS / 16));
   xrender_surf_blend(disp, surf_img, surf_win, 0, 0, w, h, 0);
   count++;
}

void
test_over_off_scale_general_nearest_x(void)
{
   int w, h;
   
   w = 1 + ((surf_img->w * count) / (REPS / 16));
   h = 1 + ((surf_img->h * count) / (REPS / 16));
   xrender_surf_blend(disp, surf_img, surf_off, 0, 0, w, h, 0);
   count++;
}

void
test_over_scale_general_smooth_x(void)
{
   int w, h;
   
   w = 1 + ((surf_img->w * count) / (REPS / 16));
   h = 1 + ((surf_img->h * count) / (REPS / 16));
   xrender_surf_blend(disp, surf_img, surf_win, 0, 0, w, h, 1);
   count++;
}

void
test_over_off_scale_general_smooth_x(void)
{
   int w, h;
   
   w = 1 + ((surf_img->w * count) / (REPS / 16));
   h = 1 + ((surf_img->h * count) / (REPS / 16));
   xrender_surf_blend(disp, surf_img, surf_off, 0, 0, w, h, 1);
   count++;
}

void
main_loop(void)
{
   /* printf query filters */
   printf("Available XRENDER filters:\n");
     {
	int i;
	XFilters *flt;
	
	flt = XRenderQueryFilters(disp, win);
	for (i = 0; i < flt->nfilter; i++) printf("%s\n", flt->filter[i]);
     }

   printf("Setup...\n");
   /* setup */
   surf_win = xrender_surf_adopt(disp, win,
				 DefaultVisual(disp, DefaultScreen(disp)),
				 win_w, win_h);
   
   surf_off = xrender_surf_new(disp, win,
			       DefaultVisual(disp, DefaultScreen(disp)),
			       320, 320, 0);
   
   surf_img = xrender_surf_new(disp, win,
			       DefaultVisual(disp, DefaultScreen(disp)),
			       100, 100, 1);
   
   populate_from_file(disp, surf_win, "tst_opaque.png");
   populate_from_file(disp, surf_off, "tst_opaque.png");
   populate_from_file(disp, surf_img, "tst_transparent.png");
   
   srand(7);
   time_test("Test Xrender doing non-scaled Over blends", test_over_x);
   srand(7);
   time_test("Test Xrender (offscreen) doing non-scaled Over blends", test_over_off_x);
   srand(7);
   time_test("Test Xrender doing 1/2 scaled Over blends", test_over_scale_half_x);
   srand(7);
   time_test("Test Xrender (offscreen) doing 1/2 scaled Over blends", test_over_off_scale_half_x);
   srand(7);
   time_test("Test Xrender doing 2* smooth scaled Over blends", test_over_scale_double_smooth_x);
   srand(7);
   time_test("Test Xrender (offscreen) doing 2* smooth scaled Over blends", test_over_off_scale_double_smooth_x);
   srand(7);
   time_test("Test Xrender doing 2* nearest scaled Over blends", test_over_scale_double_nearest_x);
   srand(7);
   time_test("Test Xrender (offscreen) doing 2* nearest scaled Over blends", test_over_off_scale_double_nearest_x);
   srand(7);
   count = 0;
   time_test("Test Xrender doing general nearest scaled Over blends", test_over_scale_general_nearest_x);
   count = 0;
   time_test("Test Xrender (offscreen) doing general nearest scaled Over blends", test_over_off_scale_general_nearest_x);
   count = 0;
   time_test("Test Xrender doing general smooth scaled Over blends", test_over_scale_general_smooth_x);
   count = 0;
   time_test("Test Xrender (offscreen) doing general smooth scaled Over blends", test_over_off_scale_general_smooth_x);
   
   XSync(disp, False);
}

void
setup_window(void)
{
   XSetWindowAttributes att;
   XClassHint *xch;

   att.background_pixmap = None;
   att.colormap = DefaultColormap(disp, DefaultScreen(disp));
   att.border_pixel = 0;
   att.event_mask = 
     ButtonPressMask | 
     ButtonReleaseMask |
     EnterWindowMask |
     LeaveWindowMask |
     PointerMotionMask | 
     ExposureMask | 
     StructureNotifyMask | 
     KeyPressMask | 
     KeyReleaseMask;
   win = XCreateWindow(disp,
		       RootWindow(disp, DefaultScreen(disp)),
		       0, 0, win_w, win_h, 0,
		       DefaultDepth(disp, DefaultScreen(disp)),
		       InputOutput,
		       DefaultVisual(disp, DefaultScreen(disp)),
		       CWColormap | CWBorderPixel | CWEventMask | CWBackPixmap,
		       &att);
   XStoreName(disp, win, "Render Test Program");
   xch = XAllocClassHint();
   xch->res_name = "Main";
   xch->res_class = "Render_Demo";
   XSetClassHint(disp, win, xch);
   XFree(xch);
   XMapWindow(disp, win);
   XSync(disp, False);
   usleep(200000);
   XSync(disp, False);
}

int 
main(int argc, char **argv)
{
   disp = XOpenDisplay(NULL);
   if (!disp)
     {
	printf("ERROR: Cannot connect to display!\n");
	exit(-1);
     }
   g_type_init ();
   setup_window();
   main_loop();
   return 0;
}
