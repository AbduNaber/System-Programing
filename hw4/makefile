all: loganalyzer

loganalyzer: main.o buffer.o
	gcc -o LogAnalyzer main.o buffer.o -lpthread 

main.o: main.c buffer.h
	gcc -c main.c

buffer.o: buffer.c buffer.h
	gcc -c buffer.c

clean:
	rm -f *.o LogAnalyzer

error:
	./LogAnalyzer 20 4 logs/sample.log "ERROR"