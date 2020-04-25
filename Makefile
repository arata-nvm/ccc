CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)
TARGET=ccc

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): chibi.h

$(TARGET)-gen2: $(TARGET) $(SRCS) chibi.h
	./self.sh

extern.o: tests-extern
	gcc -xc -c -o extern.o tests-extern

test: $(TARGET) extern.o
	./$(TARGET) tests > tmp.s
	gcc -static -o tmp tmp.s extern.o
	./tmp

test-gen2: $(TARGET)-gen2 extern.o
	./$(TARGET)-gen2 tests > tmp.s
	gcc -static -o tmp tmp.s extern.o
	./tmp

clean:
	rm -rf $(TARGET) $(TARGET)-gen* *.o *~ tmp*

.PHONY: test clean
