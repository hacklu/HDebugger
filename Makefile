CC=gcc
#CFLAGS=-c -Wall -g
CFLAGS=-c -g
LDFLAGS=-lpthread
SOURCE=demo.c exc_request_S.c msg_reply_S.c
OBJECTS=$(SOURCE:.c=.o)
EXECUTEABLE=mydemo

INFERIOR=hello_world

all:$(SOURCE) $(EXECUTEABLE) $(INFERIOR)

$(EXECUTEABLE):$(OBJECTS)
	@$(CC) $(LDFLAGS) $(OBJECTS) -o $@
$(INFERIOR):hello_world.o
	@$(CC) $< -o $@
.c.o:
	@$(CC) $(CFLAGS) $< -o $@
clean:
	@rm -rf $(EXECUTEABLE) $(OBJECTS) hello_world.o

test:all
	./mydemo hello_world
