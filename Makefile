PREFIX ?= $(HOME)/.local
CFLAGS ?= -O2 -Wall -Wextra
FRAMEWORKS = -framework IOKit -framework CoreFoundation

aula-s75: src/aula-s75.c
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORKS)

install: aula-s75
	install -d $(PREFIX)/bin
	install -m 0755 aula-s75 $(PREFIX)/bin/aula-s75

uninstall:
	rm -f $(PREFIX)/bin/aula-s75

clean:
	rm -f aula-s75

.PHONY: install uninstall clean
