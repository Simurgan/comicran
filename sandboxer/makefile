all: cfg_maker.c jailor.c
	gcc -o cfg_maker cfg_maker.c -lconfig
	gcc -o jailor jailor.c -lconfig
	./cfg_maker base_config.cfg full_config.cfg
	# sudo ./jailor full_config.cfg

clean:
	rm -f cfg_maker jailor
	