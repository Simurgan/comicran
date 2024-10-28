all: cfg_maker.c jailor.c
	gcc -o cfg_maker cfg_maker.c -lconfig
	gcc -o jailor jailor.c -lconfig

clean:
	rm -f cfg_maker jailor
	