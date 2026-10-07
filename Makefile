CC=i686-w64-mingw32-gcc
WINDRES=i686-w64-mingw32-windres
CC64=x86_64-w64-mingw32-gcc
WINDRES64=x86_64-w64-mingw32-windres

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

all: win32 x64

win32: ffrunner.exe

x64: ffrunner64.exe

ffrunner.res: ffrunner.rc
	$(WINDRES) ffrunner.rc -O coff -o ffrunner.res

ffrunner.exe: ffrunner.res $(SRC) $(HDR)
	$(CC) -std=c99 -pedantic -mwindows -Wl,--large-address-aware -O0 -g $(SRC) -o ffrunner.exe ffrunner.res -lwindowscodecs -lwininet -ldxgi

ffrunner64.res: ffrunner.rc
	$(WINDRES64) ffrunner.rc -O coff -o ffrunner64.res

ffrunner64.exe: ffrunner64.res $(SRC) $(HDR)
	$(CC64) -std=c99 -pedantic -mwindows -O0 -g $(SRC) -o ffrunner64.exe ffrunner64.res -lwindowscodecs -lwininet -ldxgi

gdbs:
	wine /usr/share/win32/gdbserver.exe localhost:10000 ffrunner.exe

gdbc:
	i686-w64-mingw32-gdb -x gdb.conf

clean:
	rm -rf ffrunner.exe ffrunner.res ffrunner64.exe ffrunner64.res
