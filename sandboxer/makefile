all: cfg_maker.c jailor.c jailor2.c
	gcc -o cfg_maker cfg_maker.c -lconfig
	gcc -o jailor jailor.c -lconfig
	gcc -o jailor2 jailor2.c -lconfig
	./cfg_maker base_config.cfg full_config.cfg
	# sudo ./jailor full_config.cfg

clean:
	rm -f cfg_maker jailor jailor2
	