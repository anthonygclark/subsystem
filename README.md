###  Simple Subsystems

Typically, when logical or physical modules are depenedent, a lot of boilerplate code is added to have modules react to eachother. With this, you can easily group modules into dependent groups. Note, this first-pass version was meant to solve a particular problem in a specific application. Generalizations are coming, but don't expect them yet.

See `./simple_test.cc` for  simple example.

#### Tutorial (simple_test.cc)

```c++

#include <iostream>
#include <memory>

#include "subsystem.hh"

using namespace management;

/* Example of extended IPC type */
using SubsystemIPC_Extended_Example = SubsystemIPC_Extended<int, std::string>;

//-- PARENT
struct FirstParent : ThreadedSubsystem<>
{
public:
    FirstParent(SubsystemMap & m) :
        ThreadedSubsystem("FirstParent", m, {} /* no parents */)
    { }

    void on_start() override { std::fprintf(stderr, "PARENT STARTED\n"); }
    void on_error() override { std::fprintf(stderr, "PARENT ERROR\n"); }
    void on_stop() override { std::fprintf(stderr, "PARENT STOPPING\n"); }
    void on_destroy() override {std::fprintf(stderr, "PARENT DESTROYING\n"); }
};

//-- CHILD1
struct FirstChild : ThreadedSubsystem<>
{
public:
    FirstChild(SubsystemMap & m, SubsystemParentsList parents) :
        ThreadedSubsystem("FirstChild", m, parents)
    {
        /* init members */
    }

    /* start members. If members are started at
     * init time, then maybe implement a .start() and
     * .stop() for reactive members.
     */
    void on_error() override { std::fprintf(stderr, "FIRST CHILD ERROR\n"); }

    /* put members in a stop state, nothing should
     * be destroyed yet, just waiting */
    void on_stop() override { std::fprintf(stderr, "FIRST CHILD STOPPING\n"); }
};

//-- CHILD2
struct SecondChild : ThreadedSubsystem<SubsystemIPC_Extended_Example, SecondChild>,
    helpers::extended_ipc_dispatcher<SecondChild>
{
    SecondChild(SubsystemMap & m, SubsystemParentsList parents) :
        ThreadedSubsystem("SecondChild", m, parents)
    {
    }

    /* forward the default handler for SubsystemIPC from the base class */
    using Subsystem::operator();

    bool operator() (int & i) { (void)i; return false;}
    bool operator() (std::string & i) { (void)i; return false; }
};

/* This is still required due to threading timing issues... */
#define simulate_work(ms) \
    std::this_thread::sleep_for(std::chrono::milliseconds(ms))

int main(void)
{
    std::fprintf(stderr, "Main thread TID %zu\n", std::hash<std::thread::id>()(std::this_thread::get_id()));

    SubsystemMap map{};
    FirstParent parent{map};
    FirstChild child{map, SubsystemParentsList{parent}};
    SecondChild child2{map, SubsystemParentsList{parent}};

    simulate_work(500);

    /* triggers parent.on_start() then child.on_start() */
    parent.start();

    simulate_work(200);

#ifndef NDEBUG
    std::cout << std::endl << map << std::endl;
#endif

    /* triggers parent.on_error(), then child.on_error() */
    parent.error();

    simulate_work(100);

    /* triggers parent.on_stop(), then child.on_stop() */
    parent.stop();

    simulate_work(500);

    /* triggers parent.on_destroy(), then child.on_destroy() */
    parent.destroy();

    simulate_work(100);

#ifndef NDEBUG
    std::cout << std::endl << map << std::endl;
#endif
}

```

Compiling this with:

```sh
clang++ --std=c++14 -Wall -Wextra -Werror simple_test.cc subsystem.cc -ggdb3 -I. -lpthread -o simple_test
```

Yields the following output:

```
Main thread TID 17656921981660240390
PARENT STARTED

SubsystemMap Entry -------
 KEY   : 1426063362
 STATE : RUNNING
  NAME : FirstChild
SubsystemMap Entry -------
 KEY   : 1426063363
 STATE : RUNNING
  NAME : SecondChild
SubsystemMap Entry -------
 KEY   : 1426063361
 STATE : RUNNING
  NAME : FirstParent

PARENT ERROR
FIRST CHILD ERROR
PARENT STOPPING
FIRST CHILD STOPPING
PARENT DESTROYING

SubsystemMap Entry -------
 KEY   : 1426063363
 STATE : DESTROY
  NAME : SecondChild
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
