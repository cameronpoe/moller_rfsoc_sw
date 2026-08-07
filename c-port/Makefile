# CC = gcc

# CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O3 -Iinclude -D_POSIX_C_SOURCE=200809L -pthread 
# LDFLAGS = -lm

# SRC = src/io.c src/iq_rot.c src/integrator.c  src/ddf.c src/rfsoc_processing.c main.c
# # main.c
# # main_streaming_v2.c 
# TESTS = tests/test1.c tests/test2.c tests/test3.c 
# OBJ = $(SRC:.c=.o) 
# #$(TESTS:.c=.o)
# DATA = 

# TARGET = rfsoc_test

# all: $(TARGET)

# $(TARGET): $(OBJ)
# 	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

# %.o: %.c
# 	$(CC) $(CFLAGS) -c $< -o $@

# run: $(TARGET)
# 	./$(TARGET) 

# clean:
# 	rm -f $(OBJ) $(TARGET)

# .PHONY: all run clean

CC ?= gcc

LIB := build/librfsoc_processing.so
INCLUDES := -Iinclude
CPPFLAGS ?=
CFLAGS ?= -O3 -DNDEBUG -std=c11 -fPIC -Wall -Wextra
LDFLAGS ?= -shared
LDLIBS ?= -pthread -lm


SOURCES ?= $(filter-out src/main.c src/main_streaming.c src/main_test.c,$(wildcard src/*.c))
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))

ifeq ($(strip $(SOURCES)),)
$(error No src/*.c files found. Copy this Makefile to the repository root or set SOURCES explicitly)
endif

.PHONY: all clean check

all: $(LIB)

$(LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

check: $(LIB)
	file $(LIB)
	nm -D $(LIB) | grep ' T process_dma_buffer$$'

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
