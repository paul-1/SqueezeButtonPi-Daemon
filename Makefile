CC = gcc
CFLAGS += -Wall -fPIC -std=gnu99 -s -I/usr/local/include -Wl,-rpath,/usr/local/lib
LDFLAGS = -L./lib -Wl,-rpath,/usr/local/lib -lcurl -lwiringPi
STATIC_LDFLAGS = -lpthread -ldl -lrt -lssl -lcrypto -lz -lm -lidn2 -lto -lwiringPi /usr/local/lib/libcurl.a

EXECUTABLE = sbpd
EXECUTABLE-STATIC_CURL = sbpd-static

SOURCES = control.c discovery.c GPIO.c sbpd.c servercomm.c uinput.c key_event_codes.c
DEPS = control.h discovery.h GPIO.h sbpd.h servercomm.h uinput.h

OBJECTS = $(SOURCES:.c=.o)

all: $(EXECUTABLE)

static: $(EXECUTABLE-STATIC_CURL)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	strip --strip-unneeded $(EXECUTABLE)

$(EXECUTABLE-STATIC_CURL): $(OBJECTS)
	$(CC) $(OBJECTS) $(STATIC_LDFLAGS) -o $@
	strip --strip-unneeded $(EXECUTABLE-STATIC_CURL)

$(OBJECTS): $(DEPS)

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE) $(EXECUTABLE-STATIC_CURL)
