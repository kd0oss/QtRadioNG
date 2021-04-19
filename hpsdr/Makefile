CC = gcc
CFLAGS = -g -Wall

LIBS = -lm -lpthread

default: hpsdr

hpsdr: discovered.o discovery.o new_discovery.o new_protocol.o old_discovery.o old_protocol.o radio.o receiver.o server.o transmitter.o main.o
	$(CC) $(CFLAGS) -o hpsdr discovered.o discovery.o new_discovery.o new_protocol.o old_discovery.o old_protocol.o radio.o receiver.o server.o transmitter.o main.o $(LIBS)

clean:
	$(RM) *.o *~ $(MAIN)
