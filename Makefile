CC=gcc
CFLAGS=-g -O0 -Wall -Werror -D_XOPEN_SOURCE=500
LDLIBS =-lcrypt -lpthread -ldl

.PHONY: grind debug install uninstall clean clear loc sched.tar.gz

sched: src/main.c obj/scheduler.o obj/telnet.o obj/email.o obj/sqlite3.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

obj/%.o: src/%.c
	mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

grind: sched
	valgrind --leak-check=full --show-leak-kinds=all ./sched

debug: sched
	gdb ./sched

sched.1.gz: sched.1
	gzip -c sched.1 > sched.1.gz

sched.pdf: sched.1
	man -t ./sched.1 | ps2pdf - sched.pdf

install: sched sched.1.gz
	install sched /usr/bin/
	install sched.1.gz /usr/share/man/man1/

uninstall:
	rm -f /usr/bin/sched
	rm -f /usr/share/man/man1/sched.1.gz

sched.tar.gz:
	tar -cvzf sched.tar.gz --transform 's,^,sched/,' src/* sched.1 LICENSE Makefile README db.db3 cases/*

clean:
	find . -name "*~" -delete

clear: clean
	rm -f sched.tar.gz
	rm -f sched.1.gz
	rm -f sched

loc:
	@wc `find . -name '*.c'` | tail -1
