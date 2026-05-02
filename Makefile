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
CPP=g++
CPPFLAGS=$(DEBUG) -std=c++17 -fpermissive -Wno-write-strings

INCLUDES:=-I/usr/include/libxml2
INCLUDES:=$(INCLUDES) -I/usr/include
INCLUDES:=$(INCLUDES) -I./ -I../XmlCls
LDFLAGS=$(DEBUG)
LDLIBS:=-lcrypto
# ifeq ($(STATIC),)
# else
# endif

ifeq ($(DEBUG),)
	BINDIR:=release
else
	BINDIR:=debug
endif

all: XmlCls.a

OBJECTS=

%.o:	%.cpp
	@if $(CPP) $(CPPFLAGS) $(INCLUDES) -c $^;\
		then echo "--- Compile $^: Success ---" | $(LOGGER) ;\
		else echo "--- Compile $^: FAILURE! ---" | $(LOGGER) ; exit 1; fi

XmlCls.a:	XmlCls.o
	@if ar rcs $@ $^;\
		then echo "--- Build $@: Success ---" | $(LOGGER) ;\
		else echo "--- Build $@: FAILURE! ---" | $(LOGGER) ; exit 1; fi

clean:
	@if rm -fv release/* debug/* *.o && rm -rf repo/;\
		then echo "--- $@: Success ---" | $(LOGGER) ;\
		else echo "--- $@: FAILURE! ---" | $(LOGGER) ; exit 1; fi
