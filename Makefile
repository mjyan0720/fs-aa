# Makefile for hello pass

# Path to top level of LLVM hierarchy
LEVEL = ../../..

# Name of the library to build
LIBRARYNAME = FlowSensitiveAliasAnalysis

# Make the shared library become a loadable module so the tools can
# dlopen/dlsym on the resulting library.
LOADABLE_MODULE = 1

# Include the makefile implementation stuff
include $(LEVEL)/Makefile.common

#LIBS += "-I./buddy22/src -L./buddy22/src -lbdd"
CPPFLAGS += -DDEFAULT_CLOCK=60
CFLAGS += -DDEFAULT_CLOCK=60
