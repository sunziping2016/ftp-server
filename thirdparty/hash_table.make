CFLAGS =-Wall -Wextra -Wno-sign-compare -O2

.PHONY: all

all: ../build/hash_table.o

../build/%.o: hash_table/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
