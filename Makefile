PREFIX?=$(HOME)

tfstool: tfs.c tfs.h tfstool.c
	gcc -Wall -Wextra -O2 -o $@ tfs.c tfstool.c

clean:
	rm --force tfstool

install: tfstool
	mkdir --parents "$(PREFIX)/bin/"
	rm --force "$(PREFIX)/bin/tfstool"
	cp tfstool "$(PREFIX)/bin/"

uninstall:
	rm --force "$(PREFIX)/bin/tfstool"
	rmdir --ignore-fail-on-non-empty "$(PREFIX)/bin"
