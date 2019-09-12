
Target = code_inspector

CC = g++ -std=gnu++11
CFLAG = -Wall 

Inc = ./inc/
Src = ./src/

Obj = $(patsubst %.cpp, %.o, $(wildcard $(Src)*.cpp))

$(Target):$(Obj)
	$(CC) $^ -o $@ $(CFLAG)

%.o:%.cpp
	$(CC) $^ -c -o $@ -I$(Inc) $(CFLAG)

clean:
	rm -f $(Obj)
