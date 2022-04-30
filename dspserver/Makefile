CC = gcc
CFLAGS = -g -O3 -Wall -I./wdsp -Wno-unused-function -fopenmp
LIBS = -lm -lwdsp -lrt -lssl -lcrypto -lfftw3 -lgomp -lsamplerate -levent_openssl -levent_pthreads -levent -lpthread
LIBDIR = -L./wdsp

default:	dspserver

all:	dspserver

dspserver: audiostream.o G711A.o util.o web.o hardware.o server.o dsp.o main.o
	$(CC) $(CFLAGS) -o dspserver audiostream.o G711A.o util.o web.o hardware.o server.o dsp.o main.o $(LIBDIR) $(LIBS)

clean:
	$(RM) *.o *~ $(MAIN)
