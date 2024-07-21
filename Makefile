CC=i686-w64-mingw32-gcc
WINDRES=i686-w64-mingw32-windres

SRC=\
	ffrunner.c\
	requests.c\
	graphics.c\
	logging.c\

HDR=\
	npapi/npapi.h\
	npapi/npfunctions.h\
	npapi/npruntime.h\
	npapi/nptypes.h\
	ffrunner.h\

all: ffrunner.exe

ffrunner.res: ffrunner.rc
	$(WINDRES) ffrunner.rc -O coff -o ffrunner.res

ffrunner.exe: ffrunner.res $(SRC) $(HDR)
	$(CC) -std=c99 -pedantic -Wl,--large-address-aware -O0 -g $(SRC) ffrunner.res -lwininet -o ffrunner.exe -lSDL2 -L.

gdbs:
	wine /usr/share/win32/gdbserver.exe localhost:10000 ffrunner.exe

gdbc:
	i686-w64-mingw32-gdb -x gdb.conf

clean:
	rm -rf ffrunner.exe ffrunner.res
