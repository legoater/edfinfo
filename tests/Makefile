VALGRIND := valgrind -v --track-origins=yes --tool=memcheck --leak-check=yes --show-reachable=yes --num-callers=20 --track-fds=yes

all: test

test: 
	cat ./edfinfo.raw | $(VALGRIND) ../edfinfod -o /dev/stderr -p debug --debug
	zcat ./edfinfo-20150414-091041.raw.gz | $(VALGRIND) ../edfinfod -o /dev/stderr -p notice --debug

test_conf:
	rm -f edfinfo.log
	zcat ./edfinfo-20150414-091041.raw.gz | $(VALGRIND) ../edfinfod -c ./edfinfo.conf --debug

test_sleep:
	rm -f edfinfo.log
	zcat ./edfinfo-20150414-091041.raw.gz | while read line; do echo $line; sleep .1;  done | $(VALGRIND) ../edfinfod -c ./edfinfo.conf --debug

clean: 
	rm -f edfinfo.log
