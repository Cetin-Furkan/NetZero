CC       ?= clang
MARCH    ?= native
CFLAGS   := -Wall -Wextra -O3 -march=$(MARCH) -pthread
WL_FLAGS := $(shell pkg-config --cflags --libs wayland-client 2>/dev/null)

BINS := grand_socket_bench netzero_bench wan_bloat_bench
ifneq ($(strip $(WL_FLAGS)),)
BINS += live_wayland_ping
endif

.PHONY: all clean run

all: $(BINS)

grand_socket_bench: grand_socket_bench.c
	$(CC) $(CFLAGS) $< -o $@

netzero_bench: netzero_bench.c
	$(CC) $(CFLAGS) $< -o $@

wan_bloat_bench: wan_bloat_bench.c
	$(CC) $(CFLAGS) $< -o $@

live_wayland_ping: live_wayland_ping.c
	$(CC) $(CFLAGS) $< $(WL_FLAGS) -o $@

run: all
	python3 ./run_grand_bench.py --trials 10

clean:
	rm -f grand_socket_bench netzero_bench wan_bloat_bench live_wayland_ping
