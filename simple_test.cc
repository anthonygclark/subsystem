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
