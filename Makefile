SRC = $(wildcard *.c)
BIN = $(patsubst %.c,%,$(SRC))

CFLAGS += -Wall -Wextra -g -D_FILE_OFFSET_BITS=64 $(shell pkg-config fuse --cflags)
LDLIBS += $(shell pkg-config fuse --libs)
#CFLAGS += --std=c99

all: $(BIN)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f $(BIN)
	rm -f *.o
	rm -f *~

indent:
	indent -linux -i4 -nut -ts2 *.c

.PHONY: all clean indent
