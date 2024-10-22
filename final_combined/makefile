all: cfg_maker.c jail_server.c
	gcc -o cfg_maker cfg_maker.c
	gcc -o jail_server jail_server.c -lconfig

clean:
	rm -f cfg_maker jail_server
