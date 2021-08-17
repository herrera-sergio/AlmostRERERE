SOURCES = main.c 

CFLAGS += $(shell pkg-config --cflags json-c)

LDFLAGS += $(shell pkg-config --libs json-c)

LD = gcc

OBJECTS = $(SOURCES:%.c=%.o)

default: all

all: $(EXE)

$(EXE): $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) -o  $(EXE) $(LIBS)

%.o: %.c

clean:
	-rm -f $(EXE)
	-rm -f $(OBJECTS)

almost-rerere: main.c
	$(CC) $(CFLAGS)  -o almost-rerere main.c $(LDFLAGS)

