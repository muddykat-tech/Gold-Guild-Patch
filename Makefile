CC = g++
CFLAGS = -O0 -g -std=c++11 -Iinclude -DADD_EXPORTS -fpermissive -m32
LDFLAGS = -m32 -shared -static-libgcc -static-libstdc++ -s -Wl,--subsystem,windows

all: bin/patch.asi

clean:
	rm -rf obj bin lib

bin obj:
	mkdir -p $@

bin/patch.asi: obj/patch.o | bin
	$(CC) -o $@ $< $(LDFLAGS)

obj/patch.o: src/patch.cpp | obj
	$(CC) $(CFLAGS) -c $< -o $@