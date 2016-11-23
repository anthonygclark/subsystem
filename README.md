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
    FirstParent() :
        ThreadedSubsystem("FirstParent", {} /* no parents */)
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
    FirstChild(SubsystemParentsList parents) :
        ThreadedSubsystem("FirstChild", parents)
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

std::unique_ptr<FirstParent> parent;
std::unique_ptr<FirstChild> child;

#define simulate_work(ms) \
    std::this_thread::sleep_for(std::chrono::milliseconds(ms))

int main(void)
{
    std::fprintf(stderr, "Main thread TID %zu\n", std::hash<std::thread::id>()(std::this_thread::get_id()));

    init_system_state(2);
    parent = std::make_unique<FirstParent>();
    child = std::make_unique<FirstChild>(SubsystemParentsList{*parent.get()});

    simulate_work(500);

    parent->start();
    /* triggers parent.on_start() then child.on_start() */

    simulate_work(100);

    parent->error();
    /* triggers parent.on_error(), then child.on_error() */

    simulate_work(100);

    parent->stop();
    /* triggers parent.on_stop(), then child.on_stop() */

    simulate_work(100);

    parent->destroy();
    /* triggers parent.on_destroy(), then child.on_destroy() */

    simulate_work(100);

    parent.reset();
    child.reset();

    return 0;
}
```

Compiling this with:

```sh
clang++ --std=c++14 -Wall -Wextra -Werror simple_test.cc subsystem.cc -ggdb3 -I. -lpthread -lrt
```

Yields the following output:

```
Main thread TID 11136472679203368661
(subsystem.cc:207 (tid:11136472679203368661     ), Subsystem                ) (FirstParent    ) DEBUG: Creating 'FirstParent' Subsystem with tag 55000000
(subsystem.cc:207 (tid:11136472679203368661     ), Subsystem                ) (FirstChild     ) DEBUG: Creating 'FirstChild' Subsystem with tag 55000001
(subsystem.cc:352 (tid:11136472679203368661     ), add_parent               ) (FirstChild     ) DEBUG: Inserting Parent 0x55000000
(subsystem.cc:306 (tid:11136472679203368661     ), add_child                ) (FirstParent    ) DEBUG: Inserting Child 0x55000001
PARENT STARTED
(subsystem.cc:389 (tid:1283900022735240788      ), commit_state             ) (FirstParent    ) DEBUG: Subsystem changed state INIT->RUNNING
(subsystem.cc:389 (tid:279770860684962401       ), commit_state             ) (FirstChild     ) DEBUG: Subsystem changed state INIT->RUNNING
PARENT ERROR
(subsystem.cc:389 (tid:1283900022735240788      ), commit_state             ) (FirstParent    ) DEBUG: Subsystem changed state RUNNING->ERROR
CHILD ERROR
PARENT STOPPING
(subsystem.cc:389 (tid:1283900022735240788      ), commit_state             ) (FirstParent    ) DEBUG: Subsystem changed state ERROR->STOPPED
(subsystem.cc:389 (tid:1283900022735240788      ), commit_state             ) (FirstParent    ) DEBUG: Subsystem changed state STOPPED->DESTROY
(subsystem.cc:389 (tid:279770860684962401       ), commit_state             ) (FirstChild     ) DEBUG: Subsystem changed state RUNNING->ERROR
CHILD STOPPING
(subsystem.cc:389 (tid:279770860684962401       ), commit_state             ) (FirstChild     ) DEBUG: Subsystem changed state ERROR->STOPPED
(subsystem.cc:389 (tid:279770860684962401       ), commit_state             ) (FirstChild     ) DEBUG: Subsystem changed state STOPPED->DESTROY
(subsystem.cc:554 (tid:11136472679203368661     ), ~ThreadedSubsystem       ) (FirstParent    ) DEBUG: Done with thread
(subsystem.cc:554 (tid:11136472679203368661     ), ~ThreadedSubsystem       ) (FirstChild     ) DEBUG: Done with thread
```

*Sorry about the wrapping, debug is verbose*



#### TODO

1. Remove need for threads and mutexs. It would be nice to have a version that works with single threaded applications - though this first pass wasn't intended for that.
2. Lower requirement of the STL for containers and prefer fixed sized containers for queues, etc.
3. Support std::allocators and other abstractions.
4. Maybe switch ThreadsafeQueue with a priority queue / max heap version.
5. Remove DEBUG_PRINT in favor of a logging abstraction i.e., `extern void log_message(...)`
6. Remove namespace-level `SystemState` instance.
