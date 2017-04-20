all:
	clang++ --std=c++1z -Wall -Wextra -Werror test.cc subsystem.cc -ggdb3 -I. -lpthread -lrt -o test
	clang++ --std=c++1z -Wall -Wextra -Werror simple_test.cc subsystem.cc -ggdb3 -I. -lpthread -lrt -o simple_test

release:
	clang++ --std=c++1z -Wall -Wextra -Werror test.cc subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt -o test
	clang++ --std=c++1z -Wall -Wextra -Werror simple_test.cc subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt -o simple_test

clean:
	$(RM) test simple_test
