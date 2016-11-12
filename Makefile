#all:
#	clang++ --std=c++11 error.cpp log_simple.cpp main.cpp subsystem.cpp util.cpp -ggdb3 -I. -lpthread -lrt  -DPIXNET_LOG_LEVEL=0xff /usr/lib/libboost_system.a /usr/lib/libboost_thread.a
#release:
#	clang++ --std=c++11 error.cpp log_simple.cpp main.cpp subsystem.cpp util.cpp -Ofast -I. -lpthread -lrt -DPIXNET_LOG_LEVEL=1 /usr/lib/libboost_system.a /usr/lib/libboost_thread.a

all:
	clang++ --std=c++14 -Wall -Wextra -Werror main.cpp subsystem.cc -ggdb3 -I. -lpthread -lrt
release:
	clang++ --std=c++14 -Wall -Wextra main.cpp subsystem.cc -Ofast -I. -DNDEBUG -lpthread -lrt
