#
# This file is part of RGBDS.
#
# Copyright (c) 1997-2018, Carsten Sorensen and RGBDS contributors.
#
# SPDX-License-Identifier: MIT
#

.SUFFIXES:
.SUFFIXES: .h .y .c .cpp .o

.PHONY: all clean install checkcodebase checkpatch checkdiff develop debug mingw32 mingw64 wine-shim dist

# User-defined variables

Q		:= @
PREFIX		:= /usr/local
bindir		:= ${PREFIX}/bin
mandir		:= ${PREFIX}/share/man
STRIP		:= -s
BINMODE		:= 755
MANMODE		:= 644
CHECKPATCH	:= ../linux/scripts/checkpatch.pl

# Other variables

PKG_CONFIG	:= pkg-config
PNGCFLAGS	:= `${PKG_CONFIG} --cflags libpng`
PNGLDFLAGS	:= `${PKG_CONFIG} --libs-only-L libpng`
PNGLDLIBS	:= `${PKG_CONFIG} --libs-only-l libpng`

# Note: if this comes up empty, `version.c` will automatically fall back to last release number
VERSION_STRING	:= `git describe --tags --dirty --always 2>/dev/null`

WARNFLAGS	:= -Wall -pedantic

# Overridable CFLAGS
CFLAGS		?= -O3 -flto=auto -DNDEBUG
CXXFLAGS	?= -O3 -flto=auto -DNDEBUG
# Non-overridable CFLAGS
# _ISOC11_SOURCE is required on certain platforms to get C11 on top of the C99-based POSIX 2008
REALCFLAGS	:= ${CFLAGS} ${WARNFLAGS} -std=gnu11 -I include \
		   -D_POSIX_C_SOURCE=200809L -D_ISOC11_SOURCE
REALCXXFLAGS	:= ${CXXFLAGS} ${WARNFLAGS} -std=c++17 -I include \
		   -D_POSIX_C_SOURCE=200809L -fno-exceptions -fno-rtti
# Overridable LDFLAGS
LDFLAGS		?=
# Non-overridable LDFLAGS
REALLDFLAGS	:= ${LDFLAGS} ${WARNFLAGS} \
		   -DBUILD_VERSION_STRING=\"${VERSION_STRING}\"

YFLAGS		?= -Wall

BISON		:= bison
RM		:= rm -rf

# Used for checking pull requests
BASE_REF	:= origin/master

# Rules to build the RGBDS binaries

all: rgbasm rgblink rgbfix rgbgfx

rgbasm_obj := \
	src/asm/charmap.o \
	src/asm/fixpoint.o \
	src/asm/format.o \
	src/asm/fstack.o \
	src/asm/lexer.o \
	src/asm/macro.o \
	src/asm/main.o \
	src/asm/opt.o \
	src/asm/output.o \
	src/asm/parser.o \
	src/asm/rpn.o \
	src/asm/section.o \
	src/asm/symbol.o \
	src/asm/util.o \
	src/asm/warning.o \
	src/extern/getopt.o \
	src/extern/utf8decoder.o \
	src/error.o \
	src/hashmap.o \
	src/linkdefs.o \
	src/opmath.o

src/asm/lexer.o src/asm/main.o: src/asm/parser.h

rgblink_obj := \
	src/link/assign.o \
	src/link/main.o \
	src/link/object.o \
	src/link/output.o \
	src/link/patch.o \
	src/link/script.o \
	src/link/sdas_obj.o \
	src/link/section.o \
	src/link/symbol.o \
	src/extern/getopt.o \
	src/extern/utf8decoder.o \
	src/error.o \
	src/hashmap.o \
	src/linkdefs.o \
	src/opmath.o

rgbfix_obj := \
	src/fix/main.o \
	src/extern/getopt.o \
	src/error.o

rgbgfx_obj := \
	src/gfx/main.o \
	src/gfx/pal_packing.o \
	src/gfx/pal_sorting.o \
	src/gfx/pal_spec.o \
	src/gfx/process.o \
	src/gfx/proto_palette.o \
	src/gfx/reverse.o \
	src/gfx/rgba.o \
	src/extern/getopt.o \
	src/error.o

rgbasm: ${rgbasm_obj}
	$Q${CC} ${REALLDFLAGS} -o $@ ${rgbasm_obj} ${REALCFLAGS} src/version.c -lm

rgblink: ${rgblink_obj}
	$Q${CC} ${REALLDFLAGS} -o $@ ${rgblink_obj} ${REALCFLAGS} src/version.c

rgbfix: ${rgbfix_obj}
	$Q${CC} ${REALLDFLAGS} -o $@ ${rgbfix_obj} ${REALCFLAGS} src/version.c

rgbgfx: ${rgbgfx_obj}
	$Q${CXX} ${REALLDFLAGS} ${PNGLDFLAGS} -o $@ ${rgbgfx_obj} ${REALCXXFLAGS} -x c++ src/version.c ${PNGLDLIBS}

test/gfx/randtilegen: test/gfx/randtilegen.c
	$Q${CC} ${REALLDFLAGS} ${PNGLDFLAGS} -o $@ $^ ${REALCFLAGS} ${PNGCFLAGS} ${PNGLDLIBS}

test/gfx/rgbgfx_test: test/gfx/rgbgfx_test.cpp
	$Q${CXX} ${REALLDFLAGS} ${PNGLDFLAGS} -o $@ $^ ${REALCXXFLAGS} ${PNGLDLIBS}

# Rules to process files

# We want the Bison invocation to pass through our rules, not default ones
.y.o:

# Bison-generated C files have an accompanying header
src/asm/parser.h: src/asm/parser.c
	$Qtouch $@

src/asm/parser.c: src/asm/parser.y
	$QDEFS=; \
	add_flag(){ \
		if src/check_bison_ver.sh $$1 $$2; then \
			DEFS="-D$$3 $$DEFS"; \
		fi \
	}; \
	add_flag 3 5 api.token.raw=true; \
	add_flag 3 6 parse.error=detailed; \
	add_flag 3 0 parse.error=verbose; \
	add_flag 3 0 parse.lac=full; \
	add_flag 3 0 lr.type=ielr; \
	echo "DEFS=$$DEFS"; \
	${BISON} $$DEFS -d ${YFLAGS} -o $@ $<

.c.o:
	$Q${CC} ${REALCFLAGS} -c -o $@ $<

.cpp.o:
	$Q${CXX} ${REALCXXFLAGS} ${PNGCFLAGS} -c -o $@ $<

# Target used to remove all files generated by other Makefile targets

clean:
	$Q${RM} rgbasm rgbasm.exe
	$Q${RM} rgblink rgblink.exe
	$Q${RM} rgbfix rgbfix.exe
	$Q${RM} rgbgfx rgbgfx.exe
	$Qfind src/ -name "*.o" -exec rm {} \;
	$Q${RM} rgbshim.sh
	$Q${RM} src/asm/parser.c src/asm/parser.h
	$Q${RM} test/gfx/randtilegen test/gfx/rgbgfx_test

# Target used to install the binaries and man pages.

install: all
	$Qmkdir -p ${DESTDIR}${bindir}
	$Qinstall ${STRIP} -m ${BINMODE} rgbasm ${DESTDIR}${bindir}/rgbasm
	$Qinstall ${STRIP} -m ${BINMODE} rgbfix ${DESTDIR}${bindir}/rgbfix
	$Qinstall ${STRIP} -m ${BINMODE} rgblink ${DESTDIR}${bindir}/rgblink
	$Qinstall ${STRIP} -m ${BINMODE} rgbgfx ${DESTDIR}${bindir}/rgbgfx
	$Qmkdir -p ${DESTDIR}${mandir}/man1 ${DESTDIR}${mandir}/man5 ${DESTDIR}${mandir}/man7
	$Qinstall -m ${MANMODE} man/rgbds.7 ${DESTDIR}${mandir}/man7/rgbds.7
	$Qinstall -m ${MANMODE} man/gbz80.7 ${DESTDIR}${mandir}/man7/gbz80.7
	$Qinstall -m ${MANMODE} man/rgbds.5 ${DESTDIR}${mandir}/man5/rgbds.5
	$Qinstall -m ${MANMODE} man/rgbasm.1 ${DESTDIR}${mandir}/man1/rgbasm.1
	$Qinstall -m ${MANMODE} man/rgbasm.5 ${DESTDIR}${mandir}/man5/rgbasm.5
	$Qinstall -m ${MANMODE} man/rgbfix.1 ${DESTDIR}${mandir}/man1/rgbfix.1
	$Qinstall -m ${MANMODE} man/rgblink.1 ${DESTDIR}${mandir}/man1/rgblink.1
	$Qinstall -m ${MANMODE} man/rgblink.5 ${DESTDIR}${mandir}/man5/rgblink.5
	$Qinstall -m ${MANMODE} man/rgbgfx.1 ${DESTDIR}${mandir}/man1/rgbgfx.1

# Target used to check the coding style of the whole codebase.
# `extern/` is excluded, as it contains external code that should not be patched
# to meet our coding style, so applying upstream patches is easier.
# `.y` files aren't checked, unfortunately...

checkcodebase:
	$Qfor file in `git ls-files | grep -E '(\.c|\.h)$$' | grep -Ev '(src|include)/extern/'`; do	\
		${CHECKPATCH} -f "$$file";					\
	done

# Target used to check the coding style of the patches from the upstream branch
# to the HEAD. Runs checkpatch once for each commit between the current HEAD and
# the first common commit between the HEAD and origin/master.
# `.y` files aren't checked, unfortunately...

checkpatch:
	$QCOMMON_COMMIT=`git merge-base HEAD ${BASE_REF}`;		\
	for commit in `git rev-list $$COMMON_COMMIT..HEAD`; do		\
		echo "[*] Analyzing commit '$$commit'";			\
		git format-patch --stdout "$$commit~..$$commit"		\
			-- src include '!src/extern' '!include/extern'	\
			| ${CHECKPATCH} - || true;			\
	done

# Target used to check for suspiciously missing changed files.

checkdiff:
	$Qcontrib/checkdiff.bash `git merge-base HEAD ${BASE_REF}`

# This target is used during development in order to prevent adding new issues
# to the source code. All warnings are treated as errors in order to block the
# compilation and make the continous integration infrastructure return failure.
# The rationale for some of the flags is documented in the CMakeLists.

develop:
	$Qenv ${MAKE} WARNFLAGS="-Werror -Wextra \
		-Walloc-zero -Wcast-align -Wcast-qual -Wduplicated-branches -Wduplicated-cond \
		-Wfloat-equal -Wlogical-op -Wnull-dereference -Wshift-overflow=2 \
		-Wstringop-overflow=4 -Wstrict-overflow=5 -Wundef -Wuninitialized -Wunused \
		-Wshadow \
		-Wformat=2 -Wformat-overflow=2 -Wformat-truncation=1 \
		-Wno-format-nonliteral \
		-Wno-type-limits -Wno-tautological-constant-out-of-range-compare \
		-Wvla \
		-Wno-unknown-warning-option \
		-fsanitize=shift -fsanitize=integer-divide-by-zero \
		-fsanitize=unreachable -fsanitize=vla-bound \
		-fsanitize=signed-integer-overflow -fsanitize=bounds \
		-fsanitize=object-size -fsanitize=bool -fsanitize=enum \
		-fsanitize=alignment -fsanitize=null -fsanitize=address" \
		CFLAGS="-ggdb3 -Og -fno-omit-frame-pointer -fno-optimize-sibling-calls" \
		CXXFLAGS="-ggdb3 -Og -fno-omit-frame-pointer -fno-optimize-sibling-calls"

# This target is used during development in order to more easily debug with gdb.

debug:
	$Qenv ${MAKE} \
		CFLAGS="-ggdb3 -Og -fno-omit-frame-pointer -fno-optimize-sibling-calls" \
		CXXFLAGS="-ggdb3 -Og -fno-omit-frame-pointer -fno-optimize-sibling-calls"

# Targets for the project maintainer to easily create Windows exes.
# This is not for Windows users!
# If you're building on Windows with Cygwin or Mingw, just follow the Unix
# install instructions instead.

mingw32:
	$Q${MAKE} all test/gfx/randtilegen test/gfx/rgbgfx_test \
		CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++ \
		BISON=bison PKG_CONFIG="PKG_CONFIG_SYSROOT_DIR=/usr/i686-w64-mingw32 pkg-config"

mingw64:
	$Q${MAKE} all test/gfx/randtilegen test/gfx/rgbgfx_test \
		CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ \
		BISON=bison PKG_CONFIG="PKG_CONFIG_SYSROOT_DIR=/usr/x86_64-w64-mingw32 pkg-config"

wine-shim:
	$Qecho '#!/bin/bash' > rgbshim.sh
	$Qecho 'WINEDEBUG=-all wine $$0.exe "$${@:1}"' >> rgbshim.sh
	$Qchmod +x rgbshim.sh
	$Qln -s rgbshim.sh rgbasm
	$Qln -s rgbshim.sh rgblink
	$Qln -s rgbshim.sh rgbfix
	$Qln -s rgbshim.sh rgbgfx

# Target for the project maintainer to produce distributable release tarballs
# of the source code.

dist:
	$Qgit ls-files | sed s~^~$${PWD##*/}/~ \
	  | tar -czf rgbds-`git describe --tags | cut -c 2-`.tar.gz -C .. -T -
