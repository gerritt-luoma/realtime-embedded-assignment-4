# Directories
INCLUDE_DIRS =
LIB_DIRS    =

# Compiler & flags
CC      = gcc
CFLAGS  = $(addprefix -I,$(INCLUDE_DIRS)) $(CDEFS)
LDFLAGS = $(addprefix -L,$(LIB_DIRS))
LIBS    = -lpthread

# Sources & targets
SRCS        = sharpen.c sharpen-240.c
OBJS        = $(SRCS:.c=.o)
EXECUTABLES = sharpen sharpen-240

.PHONY: all clean depend

all: $(EXECUTABLES)

# Link each executable from its .o
%: %.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	-rm -f *.o *~ $(EXECUTABLES)

depend:
	@echo "Running makedepend (if installed)..."
	@makedepend -- $(CFLAGS) -- $(SRCS) 2>/dev/null || true

# Compile .c to .o
.c.o:
	$(CC) $(CFLAGS) -c $<