all:
	clang++ --std=c++11 -Wall -Wextra -Werror simple_test.cc subsystem.cc -ggdb3 -I. -lpthread -lrt -o simple_test

release:
	clang++ --std=c++11 -Wall -Wextra -Werror simple_test.cc subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt -o simple_test

clean:
	$(RM) simple_test
