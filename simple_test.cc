#include <iostream>
#include "subsystem.hh"
#include "threadsafe_queue.hh"
#include <csignal>

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

    void on_start() override {
        /* start members. If members are started at 
         * init time, then maybe implement a .start() and
         * .stop() for reactive members.
         */
    }

    void on_destroy() override { }

    void on_error() override {
        std::fprintf(stderr, "ERROR CASE!\n");
        /* maybe do this... */
        //stop();
    }

    void on_stop() override {
        /* put members in a stop state, nothing should
         * be destroyed yet, just waiting */
        std::fprintf(stderr, "STOPPPPPPING!\n");
    }
};

std::unique_ptr<FirstParent> parent;
std::unique_ptr<FirstChild> child;

int main(void)
{
    std::fprintf(stderr, "Main thread TID %zu\n", std::hash<std::thread::id>()(std::this_thread::get_id()));

    bool s = false;
    init_system_state(2);
    parent = std::make_unique<FirstParent>();
    child = std::make_unique<FirstChild>(SubsystemParentsList{*parent.get()});

    std::signal(SIGUSR1, [](int sig) { (void)sig; if(parent) parent->force_signal(); if(child) child->force_signal(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    parent->start(); 
    /* triggers parent.on_start() then child.on_start() */
    if (s) std::this_thread::sleep_for(std::chrono::seconds(1));

    parent->error();
    /* triggers parent.on_error(), then child.on_error() */
    if (s) std::this_thread::sleep_for(std::chrono::seconds(1));

    parent->stop();
    /* triggers parent.on_stop(), then child.on_stop() */
    if (s) std::this_thread::sleep_for(std::chrono::seconds(1));

    parent->destroy();
    ///* triggers parent.on_destroy(), then child.on_destroy() */
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    if(parent) 
        parent->force_signal();
    
    if(child)
        child->force_signal();
    
    parent.reset(); 
    child.reset();
    
    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 5;
}

