
/**
 * @file test.cc
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 */

#include <thread>
#include <iostream>

#include "subsystem.hh"

using namespace management;

#define SIM_MS(X) std::this_thread::sleep_for(std::chrono::milliseconds(X))
#define SIM_S(X) std::this_thread::sleep_for(std::chrono::seconds(X))

struct Os_Subsystem : ThreadedSubsystem
{
    Os_Subsystem() :
        ThreadedSubsystem("OS", {})
    {
        SIM_MS(300);
    }

    void on_start() override {
        SIM_MS(200);
    }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Foo_Subsystem : ThreadedSubsystem
{
    explicit Foo_Subsystem(SubsystemParentsList parents) :
        ThreadedSubsystem("FOO", parents)
    { }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Bar_Subsystem : ThreadedSubsystem
{
    explicit Bar_Subsystem(SubsystemParentsList parents) :
       ThreadedSubsystem("BAR", parents)
    { }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};


void regular_test()
{
    std::cout << "regular_test: " << std::hash<std::thread::id>()(std::this_thread::get_id()) << std::endl;

    // create
    Os_Subsystem os;
    Foo_Subsystem foo{os};
    Bar_Subsystem bar{os};

    os.start();
    SIM_S(1);

    assert(foo.get_state() == RUNNING);
    std::printf(">> ALL SUBSYSTEMS STARTED\n");

    SIM_MS(100);

    std::printf(">> TRIGGERING ERROR ON THE 'OS' SUBSYSTEM\n");
    os.error();

    SIM_MS(100);

    std::printf(">> RESTARTING THE 'OS' SUBSYSTEM\n");
    os.start();

    SIM_MS(100);

    std::printf(">> Destroying OS\n");
    os.destroy();
    SIM_MS(100);

    std::printf(">> Destroying Foo\n");
    foo.destroy();
    SIM_MS(100);

    std::printf(">> Destroying Bar\n");
    bar.destroy();
    SIM_MS(100);

    foo.force_signal(); bar.force_signal(); os.force_signal();
}

int main()
{
    init_system_state(sizes::default_max_subsystem_count);
    regular_test();
}

