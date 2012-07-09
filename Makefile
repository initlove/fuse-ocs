all:
	gcc -Wall `pkg-config --cflags --libs fuse rest-0.7 json-glib-1.0 ` fuse_ocs.c -o fuse_ocs
test:
	gcc -Wall `pkg-config --cflags --libs  fuse rest-0.7 json-glib-1.0` test.c -o test
clean:
	rm fuse_ocs test
