SRC=\
	ffrunner.c\
	requests.c\
	graphics.c\

HDR=\
	npapi/npapi.h\
	npapi/npfunctions.h\
	npapi/npruntime.h\
	npapi/nptypes.h\
	ffrunner.h\

all: ffrunner.exe

ffrunner.exe: $(SRC) $(HDR)
	i686-w64-mingw32-gcc -std=c99 -pedantic -O0 -g $(SRC) -o ffrunner.exe

gdbs:
	wine /usr/share/win32/gdbserver.exe localhost:10000 ffrunner.exe

gdbc:
	i686-w64-mingw32-gdb -x gdb.conf

clean:
	rm -rf ffrunner.exe
