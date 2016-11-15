###  Simple Subsystems

Typically, when logical or physical modules are depenedent, a lot of boilerplate code is added to have modules react to eachother. With this, you can easily group modules into dependent groups. Note, this first-pass version was meant to solve a particular problem in a specific application. Generalizations are coming, but don't expect them yet.

See `./simple_test.cc` for  simple example.

#### Tutorial

```c++
#include <cstdio>
#include "subsystem.hh"

using namespace management;

class FirstParent : ThreadedSubsystem
{
public:
  FirstParent() :
      ThreadedSubsystem("FirstParent", {} /* no parents */)
  { }
  /* no overridden impls */
};

class FirstChild : ThreadedSubsystem
{
public:
  FirstChild(SubsystemParentsList parents) :
      ThreadedSubsystem("FirstChild", parents)
  {
  	/* init members */
  }
  
  void on_start() override {
	/* start members. If members are started at 
	 * init time, then maybe implement a .start() and
	 * .stop() for reactive members.
	 */
  }
  
  void on_error() override {
    std::printf("ERROR CASE!\n");
    /* maybe do this... */
    on_stop();
  }
 
  void on_stop() override {
    /* put members in a stop state, nothing should
     * be destroyed yet, just waiting */
  }
};

int main(void)
{
  init_system_state(2);
  FirstParent parent{};
  FirstChild child{parent};
  
  parent.start(); 
  /* triggers parent.on_start() then child.on_start() */
  
  parent.error();
  /* triggers parent.on_error(), then child.on_error() */

  parent.destroy();
  /* triggers parent.on_destroy(), then child.on_destroy() */
}
```

Compiling this with:

```sh
clang++ --std=c++14 -Wall -Wextra -Werror simple_test.cc subsystem.cc -ggdb3 -I. -lpthread -lrt
```

Yields the following output:

```
(subsystem.cc:310, add_parent) DEBUG: FirstChild: Inserting Parent 0x55000000
(subsystem.cc:275,  add_child) DEBUG: Associating FirstParent subsystem with the FirstChild subsystem
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstParent) SubsystemIPC: from:SELF, tag:FirstParent, state:RUNNING
(subsystem.cc:347, commit_state) DEBUG: FirstParent Subsystem changed state INIT->RUNNING
(subsystem.cc:354, commit_state) DEBUG: Firing to 0 parents and 1 children
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstParent) SubsystemIPC: from:SELF, tag:FirstParent, state:ERROR
(subsystem.cc:347, commit_state) DEBUG: FirstParent Subsystem changed state RUNNING->ERROR
(subsystem.cc:354, commit_state) DEBUG: Firing to 0 parents and 1 children
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstParent) SubsystemIPC: from:SELF, tag:FirstParent, state:DESTROY
(subsystem.cc:347, commit_state) DEBUG: FirstParent Subsystem changed state ERROR->DESTROY
(subsystem.cc:354, commit_state) DEBUG: Firing to 0 parents and 1 children
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstChild) SubsystemIPC: from:PARENT, tag:FirstParent, state:RUNNING
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstChild) SubsystemIPC: from:PARENT, tag:FirstParent, state:ERROR
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstChild) SubsystemIPC: from:PARENT, tag:FirstParent, state:DESTROY
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstChild) SubsystemIPC: from:SELF, tag:FirstChild, state:RUNNING
(subsystem.cc:347, commit_state) DEBUG: FirstChild Subsystem changed state INIT->RUNNING
(subsystem.cc:354, commit_state) DEBUG: Firing to 1 parents and 0 children
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstChild) SubsystemIPC: from:SELF, tag:FirstChild, state:ERROR
(subsystem.cc:347, commit_state) DEBUG: FirstChild Subsystem changed state RUNNING->ERROR
(subsystem.cc:354, commit_state) DEBUG: Firing to 1 parents and 0 children
(subsystem.cc:377, handle_bus_message) DEBUG: (FirstChild) SubsystemIPC: from:SELF, tag:FirstChild, state:STOPPED
(subsystem.cc:347, commit_state) DEBUG: FirstChild Subsystem changed state ERROR->STOPPED
(subsystem.cc:354, commit_state) DEBUG: Firing to 1 parents and 0 children
(subsystem.cc:347, commit_state) DEBUG: FirstChild Subsystem changed state STOPPED->DESTROY
(subsystem.cc:354, commit_state) DEBUG: Firing to 1 parents and 0 children
(subsystem.cc:321, operator()) DEBUG: Would commit previous state (DESTROY), skipping...
```

*Sorry about the wrapping, debug is verbose*



#### TODO

1. Remove need for threads and mutexs. It would be nice to have a version that works with single threaded applications - though this first pass wasn't intended for that.
2. Lower requirement of the STL for containers and prefer fixed sized containers for queues, etc.
3. Support std::allocators and other abstractions.
4. Maybe switch ThreadsafeQueue with a priority queue / max heap version.
5. Remove DEBUG_PRINT in favor of a logging abstraction i.e., `extern void log_message(...)`
6. Remove namespace-level `SystemState` instance.