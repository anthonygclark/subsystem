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

    void on_start() override {
        SIM_MS(200);
    }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Cam_Subsystem : Subsystem
{
    explicit Cam_Subsystem(std::initializer_list<std::reference_wrapper<Subsystem>> parents) :
        Subsystem("CAMERA", std::move(parents))
    { }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Metadata_Subsystem : Subsystem
{
    explicit Metadata_Subsystem(std::initializer_list<std::reference_wrapper<Subsystem>> parents) :
        Subsystem("METADATA", std::move(parents))
    { }

    void on_error() override {
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};


void full_test()
{
    std::cout << "full_test: " << std::hash<std::thread::id>()(std::this_thread::get_id()) << std::endl;

    // create
    Os_Subsystem os;
    Cam_Subsystem cam{os};
    Metadata_Subsystem metadata{os};

    print_system_state();

    // start threads
    std::thread os_thread([&os] { while(os.handle_bus_message()); });
    std::thread cam_thread([&cam] { while(cam.handle_bus_message()); });
    std::thread metadata_thread([&metadata] { while(metadata.handle_bus_message()); });

    os.start();
    SIM_S(1);

    assert(cam.get_state() == RUNNING);
    std::printf(">> ALL SUBSYSTEMS STARTED\n");

    SIM_MS(100);

    std::printf(">> TRIGGERING ERROR ON THE 'OS' SUBSYSTEM\n");
    os.error();

    SIM_MS(100);

    std::printf(">> RESTARTING THE 'OS' SUBSYSTEM\n");
    os.start();

    SIM_MS(100);

    std::printf(">> DESTROYING THE 'OS' SUBSYSTEM\n");
    os.destroy_now();

    print_system_state();

    SIM_MS(100);

    std::printf(">> Joining OS\n");
    os_thread.join();
    SIM_MS(100);

    std::printf(">> Joining CAM\n");
    cam.destroy_now();
    cam_thread.join();
    SIM_MS(100);

    std::printf(">> Joining Metadata\n");
    metadata.destroy_now();
    metadata_thread.join();
    SIM_MS(100);
}

int main()
{
    init_system_state(sizes::default_max_subsystem_count);
    full_test();
}

