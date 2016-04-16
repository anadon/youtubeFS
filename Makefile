CPP = g++
LIBRARIES = -lavutil -lavcodec -lavformat -lavdevice -lavfilter -ldl \
            -lasound -L/usr/lib -lpthread -lz -lswscale -lm \
            -lpthread -lSDL2
CFLAGS_DEBUG = -g -pipe -Wall -fstack-protector-all -O0 -march=native -fopenmp 
CFLAGS = -pipe -march=native -fpic -Ofast


HEADERS = 
ENCODE_SOURCE = encode.cpp
DECODE_SOURCE = decode.cpp

OBJECTS = suffixarray.o
OBJECTS_DEBUG = suffixarray-debug.o




##STANDARD BUILD########################################################

all : $(SOURCE)
	$(CPP) $(CFLAGS_DEBUG) $(LIBRARIES) $(ENCODE_SOURCE) -o encode
	$(CPP) $(CFLAGS_DEBUG) $(LIBRARIES) $(DECODE_SOURCE) -o decode


