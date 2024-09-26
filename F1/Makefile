# Variables
CC = gcc
CFLAGS = -pthread -Wall
TARGET = test.exe
SRC = test.c

# Build target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

# Clean up
clean:
	rm -f $(TARGET)

# Phony targets
.PHONY: all clean