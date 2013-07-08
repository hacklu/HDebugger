all:demo hello_world
demo:demo.c
	gcc demo.c -g -o demo -lpthread
hello_world:hello_world.c
	gcc hello_world.c -g -o hello_world
clean:
	rm -rf demo hello_world
