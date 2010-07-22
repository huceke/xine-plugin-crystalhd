XINEPLUGIN = xineplug_decode_crystalhd.so 

INSTALL       = install
XINEPLUGINDIR  = $(shell pkg-config --variable=plugindir libxine)

CFLAGS        = -O2 -march=amdfam10 -msse2 -mssse3 -mfpmath=sse -fomit-frame-pointer -pipe -DNOVDPAU -fPIC -Wall -Werror
CFLAGS        += $(shell pkg-config --cflags libxine) -I/usr/include/libcrystalhd
LIBS          += $(shell pkg-config --libs libxine) -lcrystalhd
LDFLAGS       = -shared -fvisibility=hidden -g -fPIC

CFLAGS += -DEXPORTED=__attribute__\(\(visibility\(\"default\"\)\)\)

OBJ = cpb.o nal.o h264_parser.o crystalhd.o crystalhd_hw.o crystalhd_h264.o crystalhd_vc1.o

all: clean $(XINEPLUGIN)

$(XINEPLUGIN): $(OBJ)
	$(CC) $(LDFLAGS) $(LIBS) $(OBJ) -o $@

.c: %.o
		$(CC) $(CFLAGS) $< -o $@

install: all
	@echo Installing $(XINEPLUGINDIR)/$(XINEPLUGIN)
	@-rm -rf $(XINEPLUGINDIR)/*crystalhd*
	@$(INSTALL) -D -m 0755 $(XINEPLUGIN) $(XINEPLUGINDIR)/$(XINEPLUGIN)

clean:
	@-rm -f $(XINEPLUGIN) *.o

.PHONY: $(XINEPLUGIN)
