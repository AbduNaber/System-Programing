all: loganalyzer

loganalyzer: 210104004072_main.o buffer.o
	gcc -o LogAnalyzer 210104004072_main.o buffer.o -lpthread 

210104004072_main.o: 210104004072_main.c buffer.h
	gcc -c 210104004072_main.c

buffer.o: buffer.c buffer.h
	gcc -c buffer.c

clean:
	rm -f *.o LogAnalyzer

run:
	./LogAnalyzer 20 4 logs/sample.log "ERROR"
