APPS    = japw 
PREFIX = /usr/local

 CFLAGS = -O3 -Wall
LDFLAGS = -lm

CFLAGS  += `pkg-config gtk+-2.0 gio-2.0 --cflags`
LDFLAGS += `pkg-config gtk+-2.0 gio-2.0 --libs `

all: $(APPS) 

install: 
	 cp $(APPS) $(DESTDIR)/$(PREFIX)/bin

uninstall: 
	 rm $(DESTDIR)/$(PREFIX)/bin/$(APPS)

clean:
	rm -f $(APPS)
