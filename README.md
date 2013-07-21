HDebugger
========

A debugger demo under GNU/Hurd

hello_world: the test inferior program. which contails two printf() call, by default, I set a breakpoint on the second printf(). if you change something in hello_world, please re-check the breakpoint addr by hand.(objdump -d hello_world)

how to play:
	run make test will build and execute ./mydemo hello_world

(if you don't feel bother with the noise, you can set the DEBUG=1 in demo.c)

	
