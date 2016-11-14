all:
	clang++ --std=c++14 -Wall -Wextra -Werror test.cc subsystem.cc -ggdb3 -I. -lpthread -lrt

release:
	clang++ --std=c++14 -Wall -Wextra -Werror test.cc subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt

clean:
	rm a.out
