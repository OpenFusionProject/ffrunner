SRC=\
	dumper.c

HDR=\
	../npapi/npapi.h\
	../npapi/npfunctions.h\
	../npapi/npruntime.h\
	../npapi/nptypes.h\

all: dumper.exe

dumper.exe: $(SRC) $(HDR)
	i686-w64-mingw32-gcc -O0 -g dumper.c -o dumper.exe

gdbs:
	wine /usr/share/win32/gdbserver.exe localhost:10001 dumper.exe

gdbc:
	i686-w64-mingw32-gdb -x gdb.conf

clean:
	rm -rf dumper.exe
