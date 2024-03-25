CC=gcc
EXE=sbk

compile:
	${CC} -Wall -Wextra main.c -o ${EXE} `sdl2-config --cflags --libs` -lSDL2_image -lSDL2_ttf -lm -O3 -Wno-implicit-function-declaration

clean:
	rm -rf ${EXE}