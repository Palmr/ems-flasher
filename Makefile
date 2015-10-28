PROG = test-win.exe
OBJS = ems.o main.o

CFLAGS  = -g -Wall -Werror -pedantic -std=c99

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $(PROG) $(OBJS) -llibusb-1.0
