OBJ = main.o libanax.o distranax.o projections.o anaxcurses.o
DEP = $(OBJ:.o=.d)
CC = gcc
CFLAGS = -I/opt/local/include -I/usr/include/geotiff -L/opt/local/lib -std=c99 -g -Wall -MMD -MP -D_GNU_SOURCE
LIB = -ltiff -lgeotiff -lpng -lcurl -lssl -lcrypto -lz -lpthread
BIN = anax

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LIB) -o $(BIN)

%.d: %.c

clean:
	rm -rf *.o *.d $(BIN)

