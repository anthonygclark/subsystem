all:
	clang++ --std=c++11 -Wall -Wextra -Werror simple_test.cc subsystem.cc -ggdb3 -I. -lpthread -lrt -o simple_test
	clang++ --std=c++11 -Wall -Wextra -Werror simple_test2.cc subsystem.cc -ggdb3 -I. -lpthread -lrt -o simple_test2

release:
	clang++ --std=c++11 -Wall -Wextra -Werror simple_test.cc subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt -o simple_test
	clang++ --std=c++11 -Wall -Wextra -Werror simple_test2.cc subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt -o simple_test2

clean:
	$(RM) simple_test simple_test2
