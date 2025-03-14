CC = gcc
CFLAGS = -Wall -g $(shell pkg-config --cflags gstreamer-1.0)
LDFLAGS = $(shell pkg-config --libs gstreamer-1.0)

TARGET = gst-multiplayer

# Default rule to build the program
all: $(TARGET)

# Build the program
$(TARGET): gst-multiplayer.c
	$(CC) $(CFLAGS) -o $(TARGET) gst-multiplayer.c $(LDFLAGS)

# Clean up build files
clean:
	rm -f $(TARGET)

