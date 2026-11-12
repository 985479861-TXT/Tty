# Compiler and flags
CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -D_XOPEN_SOURCE=600 $(shell pkg-config --cflags x11 xft)
LDFLAGS  = -lutil $(shell pkg-config --libs x11 xft)

# Target executable name
TARGET   = Tty

# Source and object files
SRCS     = main.c
OBJS     = $(SRCS:.c=.o)

# Default rule (runs when you type 'make')
all: $(TARGET)

# Link the object files to create the final executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compile C source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

# Declare phony targets (rules that don't represent actual physical files)
.PHONY: all clean

