all:
	clang++ --std=c++14 -Wall -Wextra -Werror main.cpp subsystem.cc -ggdb3 -I. -lpthread -lrt

release:
	clang++ --std=c++14 -Wall -Wextra -Werror main.cpp subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt

clean:
	rm a.out
