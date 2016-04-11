CPP = g++
LIBRARIES = -lavutil -lavcodec -lavformat -lavdevice -lavfilter -ldl \
            -lasound -L/usr/lib -lpthread -lz -lswscale -lm \
            -lpthread -lSDL2
CFLAGS_DEBUG = -g -pipe -Wall -fstack-protector-all -O0 -march=native -fopenmp 
CFLAGS = -pipe -march=native -fpic -Ofast


HEADERS = 
SOURCE = encode.cpp

OBJECTS = suffixarray.o
OBJECTS_DEBUG = suffixarray-debug.o




##STANDARD BUILD########################################################

all : $(SOURCE)
	$(CPP) $(CFLAGS_DEBUG) $(LIBRARIES) $(SOURCE)


