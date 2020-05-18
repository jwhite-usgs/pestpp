# This file is part of PEST++
top_builddir = ../..
include $(top_builddir)/global.mak

EXE := pestpp-sqp$(EXE_EXT)
OBJECTS := pestpp-sqp$(OBJ_EXT)


all: $(EXE)

$(EXE): $(OBJECTS)
	$(LD) $(LDFLAGS) $^ $(PESTPP_LIBS) -o $@

install: $(EXE)
	$(MKDIR) $(bindir)
	$(CP) $< $(bindir)

clean:
	$(RM) $(OBJECTS) $(EXE)

.PHONY: all install clean
