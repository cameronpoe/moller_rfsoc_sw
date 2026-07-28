CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O3  -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

SRC = src/io.c src/iq_rot.c src/integrator.c  src/ddf.c main.c
TESTS = tests/test1.c tests/test2.c tests/test3.c 
OBJ = $(SRC:.c=.o) 
#$(TESTS:.c=.o)
DATA = 

TARGET = rfsoc_test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) 

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all run clean
