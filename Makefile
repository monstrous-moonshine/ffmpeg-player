BUILD ?= debug
CFLAGS = -Wall -Wextra
ifeq ($(BUILD),debug)
    CFLAGS += -g -Og
else
    CFLAGS += -O2 -ftree-vectorize
    #CFLAGS += -DNDEBUG
    #CFLAGS += -fopt-info
endif
LDLIBS = -lSDL2 -lavformat -lavcodec -lswresample -lswscale -lavutil -lm

SRCS = app.c draw.c decode.c param.c player.c queue.c
OBJS = $(SRCS:%.c=build/%.o)
DEPS = $(OBJS:.o=.d)

player: $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

build/%.o: %.c
	@mkdir -p build
	$(CC) -c -o $@ $(CFLAGS) -MMD -MF $(@:.o=.d) $<

clean:
	$(RM) player $(OBJS) $(DEPS)

.PHONY: clean

-include $(DEPS)
