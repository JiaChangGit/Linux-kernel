# user/Makefile
CC      = gcc
CFLAGS  = -Wall -O2 -I.
TARGETS = mq_demo shm_demo benchmark

all: $(TARGETS)

mq_demo:   mq_demo.c   common.h ; $(CC) $(CFLAGS) -o $@ mq_demo.c
shm_demo:  shm_demo.c  common.h ; $(CC) $(CFLAGS) -o $@ shm_demo.c
benchmark: benchmark.c common.h ; $(CC) $(CFLAGS) -o $@ benchmark.c -lpthread

clean:
	rm -f $(TARGETS)
