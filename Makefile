all:
	gcc -g -O3 main.c -I/usr/local/include -lasound -lavformat -lavcodec -lavfilter -lavutil -lglfw -lGL
