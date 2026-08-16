# Makefile for the moller QSFP28 UDP receiver.
#
# Usage:
#   make                    Release build (-O2).
#   make debug              Debug build (-O0 -g -DDEBUG).
#   make sanitize           Debug + ASan/UBSan (slow, catches memory bugs).
#   make clean              Remove build artifacts.
#   make install            Install binary + setup script to $(PREFIX)/bin.
#   make setup              Run setup_nic.sh (requires root; override IFACE=...).
#   make check IFACE=<iface>   Show link/MTU/pause/ring for an interface.
#
# Override defaults on the command line, e.g.:
#   make CC=clang
#   make PREFIX=/opt/moller install
#   make check IFACE=enp4s0f1np1

CC       ?= gcc
CSTD     ?= -std=gnu11
WARN     ?= -Wall -Wextra -Wpedantic -Wshadow
CFLAGS   ?= -O2 $(WARN) $(CSTD)
LDFLAGS  ?= -pthread
PREFIX   ?= /usr/local
IFACE    ?= enp4s0f1np1

TARGET   := moller_recv
SRCS     := moller_recv.c
OBJS     := $(SRCS:.c=.o)

.PHONY: all debug sanitize clean install setup check

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS := -O0 -g -DDEBUG $(WARN) $(CSTD)
debug: clean $(TARGET)

sanitize: CFLAGS := -O1 -g -DDEBUG $(WARN) $(CSTD) -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS := -pthread -fsanitize=address,undefined
sanitize: clean $(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(TARGET)      $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -m 0755 setup_nic.sh   $(DESTDIR)$(PREFIX)/bin/moller-setup-nic

setup:
	sudo ./setup_nic.sh $(IFACE)

check:
	@echo "=== $(IFACE) link ==="
	@ethtool $(IFACE) 2>/dev/null | grep -E "Speed|Link detected|Duplex" || echo "(interface not found)"
	@echo "=== $(IFACE) mtu / state ==="
	@ip -o link show $(IFACE) 2>/dev/null | awk '{for(i=1;i<=NF;i++){if($$i=="mtu"){printf "mtu=%s ",$$(i+1)};if($$i=="state"){printf "state=%s ",$$(i+1)}} print ""}' || true
	@echo "=== $(IFACE) pause ==="
	@ethtool -a $(IFACE) 2>/dev/null | grep -E "RX|TX" || true
	@echo "=== $(IFACE) ring ==="
	@ethtool -g $(IFACE) 2>/dev/null | grep -E "RX:|Current" || true
	@echo "=== $(IFACE) drops ==="
	@ethtool -S $(IFACE) 2>/dev/null | grep -iE "drop|discard|error|out_of_buffer" | head -20 || true
