CC = gcc
CFLAGS = -g -Wall -Wextra -O2 -fdata-sections -ffunction-sections -I./3rdparty/log.c/src -DLOG_USE_COLOR

# Define the static library
LIBRARY = libauvfs.a
LIBRARY_OBJS = auv.o 3rdparty/log.c/src/log.o

# Define the targets (can be easily extended with more targets)
TARGETS = auvfs
TARGET_OBJS = $(patsubst %, %.o, $(TARGETS))

# All targets depend on the static library
all: $(TARGETS)

# Pattern rule for building object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rule for building executables, applied only to TARGETS
$(TARGETS): %: %.o $(LIBRARY)
	$(CC) -static $(CFLAGS) -o $@ $^

# Rule to create the static library
$(LIBRARY): $(LIBRARY_OBJS)
	ar rcs $@ $^

# Clean up build artifacts
.PHONY: clean
clean:
	rm -f $(TARGETS) $(LIBRARY) $(TARGET_OBJS) $(LIBRARY_OBJS)
