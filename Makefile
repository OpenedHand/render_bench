SRCS = render_bench.c
HEAD = 
FLGS = -g `pkg-config --cflags x11 glib-2.0 gdk-pixbuf-2.0`
LIBS = -L/usr/X11R6/lib -lm -lX11 -lXext -lXrender -lXau -lXdmcp `pkg-config --libs glib-2.0 gdk-pixbuf-2.0`
####################
OBJS = $(SRCS:.c=.o)

render_bench: $(OBJS)
	$(RM) $@
	$(CC) -o $@ $(OBJS) $(LIBS)

.c.o:
	$(CC) $(FLGS) -c $< -o $@

clean::
	rm -rf render_bench *.CKP *.ln *.BAK *.bak *.o core errs ,* *~ *.a .emacs_* tags TAGS make.log MakeOut "#"*
