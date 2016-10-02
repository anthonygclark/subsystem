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
        std::cout << __PRETTY_FUNCTION__ << " " << std::this_thread::get_id() << std::endl;
        SIM_MS(200);
    }

    void on_error() override {
        std::cout << __PRETTY_FUNCTION__ << " " << std::this_thread::get_id() << std::endl;
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Cam_Subsystem : Subsystem
{
    explicit Cam_Subsystem(std::initializer_list<std::reference_wrapper<Subsystem>> parents) :
        Subsystem("CAMERA", std::move(parents))
    { }

    void on_error() override {
        std::cout << __PRETTY_FUNCTION__ << " " << std::this_thread::get_id() << std::endl;
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};

struct Metadata_Subsystem : Subsystem
{
    explicit Metadata_Subsystem(std::initializer_list<std::reference_wrapper<Subsystem>> parents) :
        Subsystem("METADATA", std::move(parents))
    { }

    void on_error() override {
        std::cout << __PRETTY_FUNCTION__ << " " << std::this_thread::get_id() << std::endl;
        std::printf("%s: Triggering error\n", __PRETTY_FUNCTION__);
    }
};


int main()
{
    std::cout << "MAIN: " << std::this_thread::get_id() << std::endl;

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
