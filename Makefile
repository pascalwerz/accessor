ifdef DEBUG
ifneq '$(DEBUG)' '0'
    DEBUGFLAGS=-g2 -O0
else
    DEBUGFLAGS=
endif
endif

ifeq '$(findstring ;,$(PATH))' ';'
    OS := Windows
else
    OS := $(shell uname 2>/dev/null || echo Unknown)
    OS := $(patsubst CYGWIN%,Cygwin,$(OS))
    OS := $(patsubst MSYS%,MSYS,$(OS))
    OS := $(patsubst MINGW%,MSYS,$(OS))
endif

ifeq ($(OS),Darwin)
	ARCHS=-arch x86_64 -arch i386 -arch arm64
	CC=cc -O3
else
	ARCHS=
	CC=gcc -O3
endif

ACCESSOR_SOURCE_FILES=\
    accessor.c \

ifeq ($(OS),MSYS)
LIBTOOL_FLAGS=--mode=link $(CC)
else
LIBTOOL_FLAGS=
endif

ACCESSOR_OBJECT_FILES=$(patsubst %.c,%.o,$(ACCESSOR_SOURCE_FILES))

CFLAGS=-Wall -Wextra -Wno-unknown-pragmas -D TARGET_$(OS)=1

.PHONY : all clean distrib binaries build runtests

all: staticlibrary binaries

clean:
	-rm -rf *.a *.o tests *.dSYM *.tgz accessor

distrib: accessor-sources.tgz

binaries: accessor.tgz

staticlibrary: accessor.a

%.o: %.c Makefile
	$(CC) $(ARCHS) $(DEBUGFLAGS) $(CFLAGS) -o $@ -c $<

accessor.a: $(ACCESSOR_OBJECT_FILES)
	libtool $(LIBTOOL_FLAGS) -o accessor.a $^

accessor.o: accessor.c accessor.h Makefile
	$(CC) $(ARCHS) $(DEBUGFLAGS) $(CFLAGS) -c -o accessor.o accessor.c

tests: tests.c accessor.a Makefile
	$(CC) $(DEBUGFLAGS) $(CFLAGS) -o tests tests.c accessor.a

runtests: tests Makefile
	./tests

accessor-sources.tgz: accessor.h accessor.c README.md tests.c Makefile
	tar -cvzf accessor-sources.tgz accessor.h accessor.c README.md tests.c Makefile

accessor.tgz: accessor.h accessor.a Makefile
	mkdir accessor/
	cp accessor.h accessor.a accessor/
	tar -cvzf accessor.tgz accessor/accessor.h accessor/accessor.a
	rm -rf accessor/
