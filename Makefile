CC = gcc

TARGETS = main.c
OUTPUT = bluewm
INSTALLPATH = /usr/local/bin/

LIBS = -lxcb -lxcb-ewmh -lxcb-icccm -lxcb-xrm -lxcb-keysyms

build:
	$(CC) $(TARGETS) -o $(OUTPUT) $(LIBS)

clean:
	rm -f $(OUTPUT)

install:
	install -Dm755 $(OUTPUT) $(INSTALLPATH)$(OUTPUT)

uninstall:
	rm -f $(INSTALLPATH)$(OUTPUT)
