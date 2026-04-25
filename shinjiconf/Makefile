TARGET=	shinjiconf
OBJS=	./main.o

CC=	gcc
CFLAGS=	-O2

RM=	rm -f
SHELL=	/bin/sh

.PHONY: all clean

all:	$(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	$(RM) $(TARGET) $(OBJS)
