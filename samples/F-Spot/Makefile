CFLAGS = -Wall -g `pkg-config --cflags libgda-5.0`
LDFLAGS = `pkg-config --libs libgda-5.0`

all: repair-path

repair-path: repair-path.c
	$(CC) -o repair-path repair-path.c $(CFLAGS) $(LDFLAGS)

clean:
	rm -f *~
	rm -f *.o
	rm -f repair-path
