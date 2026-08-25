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
CXXFLAGS=$(DEBUG) -std=c++17 -fpermissive -Wno-write-strings

INCLUDES:=-I/usr/include/libxml2
INCLUDES:=$(INCLUDES) -I/usr/include
INCLUDES:=$(INCLUDES) -I./ -I../XmlCls -I../cpp-base64
LDFLAGS=$(DEBUG)
LDLIBS:=-lcrypto -lBase64
# ifeq ($(STATIC),)
# else
# endif

ifeq ($(DEBUG),)
	BINDIR:=release
else
	BINDIR:=debug
endif

all: libXmlCls.a

test: test.cpp libXmlCls.a
	@if $(CXX) $(CXXFLAGS) $(INCLUDES) -o $@  $^ $(LDFLAGS) -L../cpp-base64 $(LDLIBS) -lxml2;\
		then echo "--- Build test: Success ---" | $(LOGGER) ;\
		else echo "--- Build test: FAILURE! ---" | $(LOGGER) ; exit 1; fi

OBJECTS=

%.o:	%.cpp %.h
	@if $(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@;\
		then echo "--- Compile $< $*.h: Success ---" | $(LOGGER) ;\
		else echo "--- Compile $< $*.h: FAILURE! ---" | $(LOGGER) ; exit 1; fi

libXmlCls.a:	XmlCls.o
	@if ar rcs $@ $^ && ranlib $@;\
		then echo "--- Build $@: Success ---" | $(LOGGER) ;\
		else echo "--- Build $@: FAILURE! ---" | $(LOGGER) ; exit 1; fi

clean:
	@if rm -fv *.a *.o test && rm -rf repo/;\
		then echo "--- $@: Success ---" | $(LOGGER) ;\
		else echo "--- $@: FAILURE! ---" | $(LOGGER) ; exit 1; fi
