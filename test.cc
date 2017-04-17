
/**
 * @file test.cc
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 */

#include <thread>
#include <iostream>
#include <cassert>

#include <boost/variant.hpp>

#include "subsystem.hh"

using namespace management;

#define SIM_MS(X) std::this_thread::sleep_for(std::chrono::milliseconds(X))
#define SIM_S(X) std::this_thread::sleep_for(std::chrono::seconds(X))

struct Os_Subsystem : DefaultThreadedSubsystem
{
    Os_Subsystem(SubsystemMap & m) :
        DefaultThreadedSubsystem("OS", m, {})
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

struct Foo_Subsystem : DefaultThreadedSubsystem
{
    explicit Foo_Subsystem(SubsystemMap & m, SubsystemParentsList parents) :
        DefaultThreadedSubsystem("FOO", m, parents)
    { }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Bar_Subsystem : DefaultThreadedSubsystem
{
    explicit Bar_Subsystem(SubsystemMap &m, SubsystemParentsList parents) :
       DefaultThreadedSubsystem("BAR", m, parents)
    { }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

#if 1
struct MyIPC final
{
    int x;
    float y;
};

using MyVariant = boost::variant<SubsystemIPC, MyIPC, std::nullptr_t>;

struct Baz_Subsystem: ThreadedSubsystem<ThreadsafeQueue<MyVariant>>
{
    explicit Baz_Subsystem(SubsystemMap &m, SubsystemParentsList parents) :
       ThreadedSubsystem<ThreadsafeQueue<MyVariant>>("BAZ", m, parents)
    {
    }

    bool handle_ipc_message(MyVariant v)
    {
        (void)v;
        std::cout << "HEEEERR\n";
        return true;
    }
};
#endif

int main()
{
    std::cout << "Main thread TID: " << std::hash<std::thread::id>()(std::this_thread::get_id()) << std::endl;

    // create
    SubsystemMap map{};
    Baz_Subsystem b{map, {}};
    b.destroy();
#if 0
    Os_Subsystem os{map};
    Foo_Subsystem foo{map, SubsystemParentsList{os}};
    Bar_Subsystem bar{map, SubsystemParentsList{os}};

#ifndef NDEBUG
    std::cout << std::endl << map << std::endl;
#endif

    os.start();
    SIM_S(1);

    assert(foo.get_state() == SubsystemState::RUNNING);
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

#ifndef NDEBUG
    std::cout << std::endl << map << std::endl;
#endif

#endif
}

