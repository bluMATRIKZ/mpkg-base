CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -larchive
TARGET = mpkg
SOURCES = mypkg.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean

