TARGET= uvd

SRCS= uvd.cpp

PROGRAM= $(TARGET).exe
OBJS= $(SRCS:.cpp=.obj) resource.res
EXLIBS= kernel32.lib user32.lib gdi32.lib shell32.lib

CC=cl
CFLAGS= -nologo -MD -O2 -GA -EHsc -W3 -WX

RCC=rc
#RCFLAGS=/nologo

RM=del
RMFLAGS=/F /Q

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	cl $(CFLAGS) -Fe$(PROGRAM) $(OBJS) $(EXLIBS)

distclean: clean
	-$(RM) $(RMFLAGS) $(PROGRAM)

clean:
	-$(RM) $(RMFLAGS) $(OBJS)

.cpp.obj:
	$(CC) $(CFLAGS) -c -Fo$@ $<

.rc.res:
	$(RCC) $(RCFLAGS) $<
