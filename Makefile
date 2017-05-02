##
# Makefile
#
# Build instructions for Simple Filesystem.
#
# Badart, Cat
# Badart, Will
# created: MAY 2017
##

CXX	      = gcc
CXX_FLAGS = -Wall -ggdb -std=gnu11

LD		  = gcc
LD_FLAGS  =

OUT  = simplefs
OBJS = shell.o fs.o disk.o

all: $(OBJS)
	$(LD) $(LD_FLAGS) $(OBJS) -o $(OUT)

%.o: src/%.c
	$(CXX) $(CXX_FLAGS) -c $^ -o $@

clean:
	rm -f $(OUT) $(OBJS)

