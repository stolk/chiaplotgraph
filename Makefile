CC ?= cc
CFLAGS +=  -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE -std=c99 -Wall -Wno-missing-braces -g
LDFLAGS += -lm

TARGET = chiaplotgraph
SRC = chiaplotgraph.c grapher.c
OBJ = $(SRC:.c=.o)

all:	$(TARGET)

$(TARGET):	$(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o $(TARGET)
	@echo All clean
