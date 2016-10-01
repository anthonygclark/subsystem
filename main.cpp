#include <thread>
#include <iostream>

#include "subsystem.hh"

using namespace management;

#define SIM_MS(X) std::this_thread::sleep_for(std::chrono::milliseconds(X))
#define SIM_S(X) std::this_thread::sleep_for(std::chrono::seconds(X))


struct Os_Subsystem : Subsystem
{
    Os_Subsystem() :
        Subsystem("OS", {})
    {
        SIM_MS(300);
    }

    void on_start() override
    {
        SIM_MS(200);
    }

    void on_error() override
    {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Cam_Subsystem : Subsystem
{
    explicit Cam_Subsystem(std::initializer_list<std::reference_wrapper<Subsystem>> parents) :
        Subsystem("CAMERA", std::move(parents))
    { }

#if 0
    void on_parent(SubsystemIPC event) override
    {
        // copy
        auto pair = m_sysstate_ref.get(event.tag);

        auto state = pair.first;
        auto * parent = pair.second;
#if 0

        if (state == ERROR)
        {
            auto & err = parent->get_last_error();

            if (err == error::code::ESYSTEM)
                    write_log<DEBUG>("%s skipping ESYSTEM\n", m_name.c_str());
        }
#endif

        if (state == RUNNING)
        {
            write_log<DEBUG>("........ CAM detected parent is RUNNING\n");
            start();
        }

        Subsystem::on_parent(event);
    }
#endif

    void on_error() override
    {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Metadata_Subsystem : Subsystem
{
    explicit Metadata_Subsystem(std::initializer_list<std::reference_wrapper<Subsystem>> parents) :
        Subsystem("METADATA", std::move(parents))
    { }

    void on_error() override
    {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};


int main()
{
    init_system_state(sizes::default_max_subsystem_count);

    // create
    Os_Subsystem * os = new Os_Subsystem{};
    Cam_Subsystem * cam = new Cam_Subsystem{*os};
    Metadata_Subsystem * metadata = new Metadata_Subsystem{*os};

    // start threads
    std::thread os_thread([&os] { while(os->handle_bus_message()); });
    std::thread cam_thread([&cam] { while(cam->handle_bus_message()); });
    std::thread metadata_thread([&metadata] { while(metadata->handle_bus_message()); });

    //SIM_MS(1000);

    os->start();
    cam->start();
    metadata->start();

    //SIM_MS(100);
    std::printf(">> ALL SUBSYSTEMS STARTED\n");

    std::string input;
    while(std::cin >> input) {
        if (input == "x")
            print_system_state();
            break;
    }

    std::printf(">> TRIGGERING ERROR ON THE 'OS' SUBSYSTEM\n");
    os->error();
    // stop regularly
    //cam->stop();
    //metadata->stop();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    while(std::cin >> input) {
        if (input == "x")
            print_system_state();
            break;
    }

    std::printf(">> RESTARTING THE 'OS' SUBSYSTEM\n");
    os->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    while(std::cin >> input) {
        if (input == "x")
            print_system_state();
            break;
    }

    std::printf(">> DELETING ALL SUBSYSTEMS\n");
    delete metadata;
    delete cam;
    delete os;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::printf(">> Joining OS\n");
    os_thread.join();

    std::printf(">> Joining CAM\n");
    cam_thread.join();

    std::printf(">> Joining Metadata\n");
    metadata_thread.join();

}
