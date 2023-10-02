include /opt/fpp/src/makefiles/common/setup.mk

all: libfpp-plugin-triksc.so
debug: all

OBJECTS_fpp_triksc_so += src/TriksCPlugin.o
LIBS_fpp_triksc_so += -L/opt/fpp/src -lfpp
CXXFLAGS_src/TriksCPlugin.o += -I/opt/fpp/src

%.o: %.cpp Makefile
	$(CCACHE) $(CC) $(CFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c $< -o $@

libfpp-plugin-triksc.so: $(OBJECTS_fpp_triksc_so) /opt/fpp/src/libfpp.so
	$(CCACHE) $(CC) -shared $(CFLAGS_$@) $(OBJECTS_fpp_triksc_so) $(LIBS_fpp_triksc_so) $(LDFLAGS) -o $@

clean:
	rm -f libfpp-plugin-triksc.so $(OBJECTS_fpp_triksc_so)


