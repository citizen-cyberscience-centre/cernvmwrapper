# This should work on Linux.  Modify as needed for other platforms.

# Change the following to match your installation
BOINC_DIR = ../..
BOINC_API_DIR = $(BOINC_DIR)/api
BOINC_LIB_DIR = $(BOINC_DIR)/lib

# CernVM-Graphics Linking
CERNVMGRAPHICS_DIR = ../CernVM-Graphics

CXXFLAGS = -g \
    -I$(BOINC_DIR) \
    -I$(BOINC_LIB_DIR) \
    -I$(BOINC_API_DIR) \
    -L$(BOINC_API_DIR) \
    -L$(BOINC_LIB_DIR) \
    -L.

ifneq ($(wildcard $(CERNVMGRAPHICS_DIR)),)
  CXXFLAGS += -DAPP_GRAPHICS \
              -I$(CERNVMGRAPHICS_DIR)
endif

PROGS = CernVMwrapper

all: $(PROGS)

libstdc++.a:
	ln -s `g++ -print-file-name=libstdc++.a`

clean:
	rm $(PROGS) *.o

distclean:
	/bin/rm -f $(PROGS) *.o libstdc++.a

CernVMwrapper: CernVMwrapper.o libstdc++.a $(BOINC_LIB_DIR)/libboinc.a $(BOINC_API_DIR)/libboinc_api.a vbox.h helper.h
	g++ $(CXXFLAGS) -o CernVMwrapper CernVMwrapper.o libstdc++.a -pthread -lboinc_api -lboinc -lz
