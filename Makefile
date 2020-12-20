TARGETS = mbs2 

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
CFLAGS = -Wall -g -O2 -Iutil -I.

util/util_sdl.o: CFLAGS += $(shell sdl2-config --cflags)
#util/util_sdl.o: CFLAGS += -DENABLE_UTIL_SDL_BUTTON_SOUND

SRC_MBS2 = mbs2.c \
           eval.c \
           cache.c \
           util/util_jpeg.c \
           util/util_misc.c \
           util/util_png.c \
           util/util_sdl.c

DEP = $(SRC_MBS2:.c=.d)

#
# build rules
#

all: $(TARGETS)

mbs2: $(SRC_MBS2:.c=.o)
	echo "char *version = \"`git log -1 --format=%h`\";" > version.c
	$(CC) -o $@ $(SRC_MBS2:.c=.o) version.c \
              -lpthread -lm -ljpeg -lpng -lSDL2 -lSDL2_ttf

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(DEP) $(DEP:.d=.o) version.c
