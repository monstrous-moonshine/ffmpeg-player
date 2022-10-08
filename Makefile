BUILD = debug
CFLAGS = -Wall -Wextra
ifeq ($(BUILD),debug)
    CFLAGS += -g -Og
else
    CFLAGS += -02 -DNDEBUG
endif
LDLIBS = -lSDL2 -lavformat -lavcodec -lswresample -lswscale -lavutil

SRCS = app.c decode.c player.c queue.c
OBJS = $(SRCS:%.c=build/%.o)

player: $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

build/%.o: %.c
	@mkdir -p build
	$(CC) -c -o $@ $(CFLAGS) $<

clean:
	$(RM) player $(OBJS)

.PHONY: clean
