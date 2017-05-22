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
struct SecondChild : ThreadedSubsystem<ThreadsafeQueue, SubsystemIPC_Extended_Example, SecondChild>,
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
