.PHONY: all clean

CFLAGS=-I include -Wall -Wextra -O2

server: $(patsubst src/%.c,build/%.o,$(wildcard src/*.c)) build/bcrypt.a build/hash_table.o
	$(CC) $(CFLAGS) -lreadline -o $@ $^

build/bcrypt.a: FORCE
	$(MAKE) -C thirdparty -f bcrypt.make

build/hash_table.o: FORCE
	$(MAKE) -C thirdparty -f hash_table.make

build/%.o: src/%.c $(wildcard include/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f build/*.o build/*.a server

FORCE:
