CFLAGS =-Wall -Wextra -O2

.PHONY: all

all: ../build/bcrypt.a

../build/bcrypt.a: ../build/bcrypt.o ../build/crypt_blowfish.o $(addprefix ../build/,bcrypt.o crypt_blowfish.o x86.o crypt_gensalt.o wrapper.o)
	$(AR) -r $@ $^

../build/%.o: bcrypt/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

../build/%.o: bcrypt/crypt_blowfish/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

../build/%.o: bcrypt/crypt_blowfish/%.S
	$(CC) $(CFLAGS) -c -o $@ $<
