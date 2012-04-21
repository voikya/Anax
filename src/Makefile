OBJ = main.o libanax.o distranax.o
DEP = $(OBJ:.o=.d)
CC = gcc
CFLAGS = -I/opt/local/include -L/opt/local/lib -std=c99 -g -Wall -MMD -MP
LIB = -ltiff -lgeotiff -lpng
BIN = anax

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LIB) $(OBJ) -o $(BIN)

%.d: %.c

clean:
	rm -rf *.o *.d $(BIN)

