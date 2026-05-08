CC = aarch64-linux-gnu-gcc
CFLAGS = -O2 -mcpu=cortex-a72 -ftree-vectorize -Wall
LDFLAGS = -lasound -lm -lpthread

TARGET = rompler
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC) wave_table.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
