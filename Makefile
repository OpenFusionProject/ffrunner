SRC=\
	ffrunner.c

HDR=\
	npapi/npapi.h\
	npapi/npfunctions.h\
	npapi/npruntime.h\
	npapi/nptypes.h\

all: ffrunner.exe

ffrunner.exe: $(SRC) $(HDR)
	i686-w64-mingw32-gcc -O0 -g ffrunner.c -o ffrunner.exe

gdbs:
	wine /usr/share/win32/gdbserver.exe localhost:10000 ffrunner.exe

gdbc:
	i686-w64-mingw32-gdb -x gdb.conf

clean:
	rm -rf ffrunner.exe
