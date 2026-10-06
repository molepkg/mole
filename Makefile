CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -pedantic -Wno-format-truncation
PREFIX ?= /usr/local

SRC = main.c recipe.c build.c deps.c den.c util.c
OBJ = $(SRC:.c=.o)

mole: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c mole.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f mole $(OBJ)

install: mole
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp mole $(DESTDIR)$(PREFIX)/bin/mole
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mole

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mole

.PHONY: clean install uninstall
