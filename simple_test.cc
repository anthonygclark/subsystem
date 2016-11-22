#include <cstdio>
#include "subsystem.hh"

using namespace management;

struct FirstParent : ThreadedSubsystem
{
public:
    FirstParent() :
        ThreadedSubsystem("FirstParent", {} /* no parents */)
        { }

    void on_start() override {
        std::fprintf(stderr, "STARTED\n");
    }

    void on_error() override {
        std::fprintf(stderr, "ERROR\n");
    }

    void on_stop() override {
        std::fprintf(stderr, "STOPPING\n");
    }
    /* no overridden impls */
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

    void on_start() override {
        /* start members. If members are started at 
         * init time, then maybe implement a .start() and
         * .stop() for reactive members.
         */
    }

    void on_error() override {
        std::fprintf(stderr, "ERROR CASE!\n");
        /* maybe do this... */
        //stop();
    }

    void on_stop() override {
        /* put members in a stop state, nothing should
         * be destroyed yet, just waiting */
    }
};

int main(void)
{
    bool sleeps = false;

    init_system_state(2);
    FirstParent parent{};
    FirstChild child{parent};

    std::this_thread::sleep_for(std::chrono::seconds(3));

    parent.start(); 
    /* triggers parent.on_start() then child.on_start() */
    if (sleeps) std::this_thread::sleep_for(std::chrono::milliseconds(300));

    parent.error();
    /* triggers parent.on_error(), then child.on_error() */
    if (sleeps) std::this_thread::sleep_for(std::chrono::milliseconds(300));

    parent.stop();
    /* triggers parent.on_stop(), then child.on_stop() */
    if (sleeps) std::this_thread::sleep_for(std::chrono::milliseconds(300));

    parent.destroy();
    /* triggers parent.on_destroy(), then child.on_destroy() */
    if (sleeps) std::this_thread::sleep_for(std::chrono::milliseconds(300));

    return 0;
}

