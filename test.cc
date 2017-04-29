
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

#if 0
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

#endif

struct MyIPC final
{
    int x;
    float y;
};

using MyVariant = SubsystemIPC_Extended<MyIPC, std::nullptr_t>;

struct Baz_Subsystem : public ThreadedSubsystem<ThreadsafeQueue<MyVariant>, Baz_Subsystem>
{
    struct variant_helper : boost::static_visitor<bool>
    {
        Baz_Subsystem & m_subsys;

        variant_helper(Baz_Subsystem & ss) :
            m_subsys(ss)
        {
        }

        bool operator() (SubsystemIPC f) const
        {
            m_subsys.handle_subsystem_ipc_message(f);
            return true;
        }

        bool operator() (MyIPC f) const
        {
            std::cout << m_subsys.get_name() << " got MyIPC: " << f.x << " " << f.y << std::endl;
            return true;
        }

        bool operator() (std::nullptr_t f) const
        {
            (void)f;
            return true;
        }
    };

    explicit Baz_Subsystem(SubsystemMap &m, SubsystemParentsList parents) :
       ThreadedSubsystem("BAZ", m, parents)
    {
    }

    void on_start() override
    {
        std::cout << m_name << " Sending MyIPC\n";
        MyIPC x{1, 3.14};

        put_message_extended(x); // self
    }

    bool handle_ipc_message(MyVariant v)
    {
       return boost::apply_visitor(variant_helper(*this), v);
    }
};

struct Baz_Subsystem2 : public ThreadedSubsystem<ThreadsafeQueue<MyVariant>, Baz_Subsystem2>
{
    struct variant_helper : boost::static_visitor<bool>
    {
        Baz_Subsystem2 & m_subsys;

        variant_helper(Baz_Subsystem2 & ss) :
            m_subsys(ss)
        {
        }

        bool operator() (SubsystemIPC f) const
        {
            m_subsys.handle_subsystem_ipc_message(f);
            return true;
        }

        bool operator() (MyIPC f) const
        {
            (void)f;
            return true;
        }

        bool operator() (std::nullptr_t f) const
        {
            (void)f;
            return true;
        }
    };

    explicit Baz_Subsystem2(SubsystemMap &m, SubsystemParentsList parents) :
       ThreadedSubsystem("BAZ2", m, parents)
    {
    }

    bool handle_ipc_message(MyVariant v)
    {
       return boost::apply_visitor(variant_helper(*this), v);
    }
};

int main()
{
    std::cout << "Main thread TID: " << std::hash<std::thread::id>()(std::this_thread::get_id()) << std::endl;

    // create
    SubsystemMap map{};
    Baz_Subsystem b{map, {}};
    Baz_Subsystem2 bb{map, {b}};

    b.start();

    SIM_MS(1);

#ifndef NDEBUG
    std::cout << map << std::endl;
#endif

    b.error();

    SIM_MS(1);

#ifndef NDEBUG
    std::cout << map << std::endl;
#endif

    b.destroy();
    bb.destroy();

    SIM_MS(1);

#ifndef NDEBUG
    std::cout << map << std::endl;
#endif

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

