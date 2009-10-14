#
# $Id: Makefile,v 1.1 2009/10/14 07:59:05 urs Exp $
#

RM      = rm -f
CFLAGS  = -Os
LDFLAGS = -s

programs = uradio

.PHONY: all
all: $(programs)

.PHONY: clean
clean:
	$(RM) $(programs) *.o core
