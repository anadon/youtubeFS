CPP = g++
CFLAGS_DEBUG = -g -pipe -Wall -fstack-protector-all -O2 -march=native -lpthread -lSDL2 -fopenmp
CFLAGS = -pipe -march=native -fpic -Ofast


HEADERS = 
SOURCE = encode.cpp

OBJECTS = suffixarray.o
OBJECTS_DEBUG = suffixarray-debug.o




##STANDARD BUILD########################################################

all : $(SOURCE)
	$(CPP) $(CFLAGS_DEBUG) $(SOURCE)


