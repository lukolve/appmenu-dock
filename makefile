All:
	gcc dock.c ini.c -o my_dock `pkg-config --cflags --libs gtk+-3.0 libwnck-3.0`
