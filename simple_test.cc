#include <iostream>
#include <memory>

#include "subsystem.hh"

using namespace management;

struct FirstParent : DefaultThreadedSubsystem
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

struct FirstChild : DefaultThreadedSubsystem
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

#ifndef NDEBUG
    std::cout << std::endl << map << std::endl;
#endif

    parent.reset();
    child.reset();

    return 0;
}
