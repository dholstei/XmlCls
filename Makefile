# 
#	XmlCls - XML classes
#	Programmer: Danny Holstein
#
LOG?=Install.log	#	Use Linux "logger" command to send logging info to this file
ifeq (null,$(shell if ls -l /dev/log  2>/dev/null > /dev/null ; then echo system; else echo null; fi))
	LOGGER?={ read LOGMSG && echo "[$@: `date`]" $$LOGMSG | tee -a $(LOG); }
else
	LOGGER?=logger --tag "[$@: `date`]" -s 2>&1 | tee -a $(LOG)
endif
CXX=g++
ifeq ($(SANITIZE),1)
    DEBUG += -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer

    DUMMY := $(shell echo 'SANITIZE enabled (ASan/LSan/UBSan)' | $(LOGGER) >&2)
    DUMMY := $(shell echo 'Run: ASAN_OPTIONS=detect_leaks=1:malloc_context_size=12 LSAN_OPTIONS=report_objects=0 ./test' | $(LOGGER) >&2)
else
    DEBUG += -O2
endif
CXXFLAGS=$(DEBUG) -std=c++17 -fpermissive -Wno-write-strings -fPIC

INCLUDES:=-I/usr/include/libxml2
INCLUDES:=$(INCLUDES) -I/usr/include
INCLUDES:=$(INCLUDES) -I./ -I../XmlCls -I../cpp-base64
LDFLAGS=$(DEBUG)
LDLIBS:=-lcrypto -lBase64 -lxml2
# ifeq ($(STATIC),)
# else
# endif

ifeq ($(DEBUG),)
	BINDIR:=release
else
	BINDIR:=debug
endif

XMLCLS_LIB := XmlClsLib.so

all: libXmlCls.a $(XMLCLS_LIB)

$(XMLCLS_LIB): XmlClsLib.o XmlCls.o Error.o
	@if $(CXX) -shared -o $@ $^ -L../cpp-base64  $(LDLIBS);\
		then echo "--- Build \"$@\": Success ---" | $(LOGGER) ;\
		else echo "--- Build \"$@\": FAILURE! ---" | $(LOGGER) ; exit 1; fi

XmlClsLib.o: XmlClsLib.cpp XmlCls.h Error.h
	@if $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) -c $< -o $@;\
		then echo "--- Build \"$@\": Success ---" | $(LOGGER) ;\
		else echo "--- Build \"$@\": FAILURE! ---" | $(LOGGER) ; exit 1; fi

test: test.cpp libXmlCls.a
	@if $(CXX) $(CXXFLAGS) $(INCLUDES) -o $@  $^ $(LDFLAGS) -L../cpp-base64 $(LDLIBS) -lxml2;\
		then echo "--- Build \"test\": Success ---" | $(LOGGER) ;\
		else echo "--- Build \"test\": FAILURE! ---" | $(LOGGER) ; exit 1; fi

OBJECTS=

%.o:	%.cpp %.h
	@if $(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@;\
		then echo "--- Compile $< $*.h: Success ---" | $(LOGGER) ;\
		else echo "--- Compile $< $*.h: FAILURE! ---" | $(LOGGER) ; exit 1; fi

libXmlCls.a:	XmlCls.o Error.o
	@if ar rcs $@ $^ && ranlib $@;\
		then echo "--- Build $@: Success ---" | $(LOGGER) ;\
		else echo "--- Build $@: FAILURE! ---" | $(LOGGER) ; exit 1; fi
	@if ln -sf $@ XmlCls.a;\
		then echo "--- Link $@: Success ---" | $(LOGGER) ;\
		else echo "--- Link $@: FAILURE! ---" | $(LOGGER) ; exit 1; fi

clean:
	@if rm -fv *.a *.o *.so test && rm -rf repo/;\
		then echo "--- $@: Success ---" | $(LOGGER) ;\
		else echo "--- $@: FAILURE! ---" | $(LOGGER) ; exit 1; fi
