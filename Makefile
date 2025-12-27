CC ?= cc
CFLAGS = -Wall -Wextra -O2 -ggdb
LIBFLAGS = -DBUF_INIT_SIZE=4 -DUSE_EXTENTION -DGAP_DEBUG -DGBF_IMPLEMENTATION

default: demo

demo: demo.c gbf.h
	$(CC) $(CFLAGS) $(LIBFLAGS) -o $@ $<

clean:
	rm -rf demo

.PHONY: clean
