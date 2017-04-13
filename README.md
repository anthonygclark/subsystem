###  Simple Subsystems

Typically, when logical or physical modules are depenedent, a lot of boilerplate code is added to have modules react to eachother. With this, you can easily group modules into dependent groups. Note, this first-pass version was meant to solve a particular problem in a specific application. Generalizations are coming, but don't expect them yet.

See `./simple_test.cc` for  simple example.

#### Tutorial (simple_test.cc)

```c++
#include <iostream>
#include <memory>

#include "subsystem.hh"

using namespace management;

struct FirstParent : ThreadedSubsystem
{
public:
    FirstParent(SubsystemMap & m) :
        ThreadedSubsystem("FirstParent", m, {} /* no parents */)
    { }

    virtual ~FirstParent() { }

    void on_start() override {
        std::fprintf(stderr, "PARENT STARTED\n");
    }

    void on_error() override {
        std::fprintf(stderr, "PARENT ERROR\n");
    }

    void on_stop() override {
        std::fprintf(stderr, "PARENT STOPPING\n");
    }

    void on_destroy() override { }
};

struct FirstChild : ThreadedSubsystem
{
public:
    FirstChild(SubsystemMap & m, SubsystemParentsList parents) :
        ThreadedSubsystem("FirstChild", m, parents)
    {
        /* init members */
    }

    virtual ~FirstChild() { }

    void on_start() override
    {
        /* start members. If members are started at
         * init time, then maybe implement a .start() and
         * .stop() for reactive members.
         */
    }

    void on_destroy() override { }

    void on_error() override
    {
        std::fprintf(stderr, "CHILD ERROR\n");
    }

    void on_stop() override
    {
        /* put members in a stop state, nothing should
         * be destroyed yet, just waiting */
        std::fprintf(stderr, "CHILD STOPPING\n");
    }
};

#define simulate_work(ms) \
    std::this_thread::sleep_for(std::chrono::milliseconds(ms))

int main(void)
{
    std::fprintf(stderr, "Main thread TID %zu\n", std::hash<std::thread::id>()(std::this_thread::get_id()));

    SubsystemMap map{};
    auto parent = std::make_unique<FirstParent>(map);
    auto child = std::make_unique<FirstChild>(map, SubsystemParentsList{*parent.get()});

    simulate_work(500);

    /* triggers parent.on_start() then child.on_start() */
    parent->start();

    simulate_work(200);

#ifndef NDEBUG
    std::cout << std::endl << map << std::endl;
#endif

    /* triggers parent.on_error(), then child.on_error() */
    parent->error();

    simulate_work(100);

    /* triggers parent.on_stop(), then child.on_stop() */
    parent->stop();

    simulate_work(100);

    /* triggers parent.on_destroy(), then child.on_destroy() */
    parent->destroy();

    simulate_work(100);

    parent.reset();
    child.reset();

#ifndef NDEBUG
    std::cout << std::endl << map << std::endl;
#endif
  
    return 0;
}

```

Compiling this with:

```sh
clang++ --std=c++14 -Wall -Wextra -Werror simple_test.cc subsystem.cc -ggdb3 -I. -lpthread -o simple_test
```

Yields the following output:

```
Main thread TID 9558670992743276620
PARENT STARTED

SubsystemMap Entry -------
 KEY   : 1426063362
 STATE : RUNNING
  NAME : FirstChild
SubsystemMap Entry -------
 KEY   : 1426063361
 STATE : RUNNING
  NAME : FirstParent

PARENT ERROR
CHILD ERROR
PARENT STOPPING
CHILD STOPPING

SubsystemMap Entry -------
 KEY   : 1426063362
 STATE : DESTROY
  NAME : FirstChild
SubsystemMap Entry -------
 KEY   : 1426063361
 STATE : DESTROY
  NAME : FirstParent
```



#### TODO

1. Remove the need for threading all together so this can be abstracted to use coroutines.
2. Lower requirement of the STL for containers and prefer fixed sized containers for queues, etc.
3. Support more type_traits.
4. Maybe switch ThreadsafeQueue with a priority queue / max heap version.
5. Replace pthread_rw_lock with std::shared_mutex or atomics (?)
6. Allow subclasses to define boost/std::variants to send over the subsystem bus so the bus can act as general IPC between subsystems (and not just for state changes).
