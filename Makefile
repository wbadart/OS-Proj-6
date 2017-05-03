##
# Makefile
#
# Build instructions for Simple Filesystem.
#
# Badart, Cat
# Badart, Will
# created: MAY 2017
##

CXX	      = /usr/bin/gcc
CXX_FLAGS = -Wall -ggdb

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

reset-images:
	curl https://www3.nd.edu/~dthain/courses/cse30341/spring2017/project6/image.5 -o data/image.5
	curl https://www3.nd.edu/~dthain/courses/cse30341/spring2017/project6/image.20 -o data/image.20
	curl https://www3.nd.edu/~dthain/courses/cse30341/spring2017/project6/image.200 -o data/image.200

