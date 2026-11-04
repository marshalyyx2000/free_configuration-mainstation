CC ?= gcc

TOP_DIR ?= ../..

INCLUDES := -I$(TOP_DIR)/include
LIBDIRS  := -L$(TOP_DIR)/lib

CFLAGS  += -Wall -Wextra -O2 $(INCLUDES)
LDFLAGS += $(LIBDIRS) -lkemyond -lgpiod -lpthread

TARGETS := \
	test_kemyond_all \
	test_setled \
	test_getled \
	test_getpowerlost \
	test_getcputemp \
	test_watchdog

all: $(TARGETS)

test_kemyond_all: test_kemyond_all.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_setled: test_setled.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_getled: test_getled.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_getpowerlost: test_getpowerlost.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_getcputemp: test_getcputemp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_watchdog: test_watchdog.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)
