CXX = g++ -fPIC -pthread
CC = gcc
NETLIBS= -lnsl

all: git-commit myhttpd daytime-server use-dlopen hello.so jj-mod.so

daytime-server : daytime-server.o
	$(CXX) -o $@ $@.o $(NETLIBS)

myhttpd : myhttpd.o
	$(CXX) -o $@ $@.o $(NETLIBS) -ldl

use-dlopen: use-dlopen.o
	$(CXX) -o $@ $@.o $(NETLIBS) -ldl

hello.so: hello.o
	ld -G -o http-root-dir/cgi-bin/hello.so hello.o

jj-mod.so: jj.o util.o
	ld -G -o http-root-dir/cgi-bin/jj-mod.so jj.o util.o

.c.o: 
	$(CC) -c $<

%.o: %.cc
	@echo 'Building $@ from $<'
	$(CXX) -o $@ -c -I. $<

.PHONY: git-commit
git-commit:
	git checkout
	git add *.cc *.h Makefile >> .local.git.out  || echo
	git commit -a -m 'Commit' >> .local.git.out || echo
	git push origin master 

.PHONY: clean
clean:
	rm -f *.o use-dlopen hello.so
	rm -f *.o daytime-server myhttpd

