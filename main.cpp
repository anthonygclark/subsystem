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


#if 0
int main(void)
{
    std::cout << "MAIN: " << std::this_thread::get_id() << std::endl;

    init_system_state(sizes::default_max_subsystem_count);

    // create
    Os_Subsystem * os = new Os_Subsystem{};
    Cam_Subsystem * cam = new Cam_Subsystem{*os};
    std::thread os_thread([&os] { while(os->handle_bus_message()); });
    std::thread cam_thread([&cam] { while(cam->handle_bus_message()); });

    os->start();

    std::string input{};
    
    while(std::cin >> input)
    {
        if (input == "x")
            print_system_state();
            break;
    }
    
    std::printf(">> deleting OS\n");
    delete os;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    print_system_state();

    std::printf(">> deleting Cam\n");
    delete cam;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    print_system_state();

    std::printf(">> Joining OS\n");
    os_thread.join();
    std::printf(">> Joining CAM\n");
    cam_thread.join();
}
#else


void full_test()
{
    std::cout << "full_test: " << std::hash<std::thread::id>()(std::this_thread::get_id()) << std::endl;

    // create
    Os_Subsystem * os = new Os_Subsystem{};
    Cam_Subsystem * cam = new Cam_Subsystem{*os};
    Metadata_Subsystem * metadata = new Metadata_Subsystem{*os};

    print_system_state();

    // start threads
    std::thread os_thread([&os] { while(os->handle_bus_message()); });
    std::thread cam_thread([&cam] { while(cam->handle_bus_message()); });
    std::thread metadata_thread([&metadata] { while(metadata->handle_bus_message()); });

    //SIM_MS(1000);

    os->start();
    //cam->start();
    //metadata->start();


    SIM_S(1);
    assert(cam->get_state() == RUNNING);
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

    os->destroy();

    print_system_state();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::printf(">> Joining OS\n");
    os->stop_bus();
    delete os;
    os_thread.join();

    std::printf(">> Joining CAM\n");
    cam->stop_bus();
    delete cam;
    cam_thread.join();

    std::printf(">> Joining Metadata\n");
    metadata->stop_bus();
    delete metadata;
    metadata_thread.join();
}

int main()
{
    init_system_state(sizes::default_max_subsystem_count);

    full_test();
    //Os_Subsystem * os = new Os_Subsystem{};
    //Cam_Subsystem * cam = new Cam_Subsystem{*os};
    //
    //print_system_state();
    //
    //std::thread cam_thread([&cam] { while(cam->handle_bus_message()); });
    //std::thread os_thread([&os] { while(os->handle_bus_message()); });

    //os->start();
    ////cam->start();

    //SIM_S(1);
    //print_system_state();

    //assert(cam->get_state() == RUNNING);

    //std::string input;
    //while(std::cin >> input) {
    //    if (input == "x")
    //        print_system_state();
    //        break;
    //}

    //delete cam;
    //delete os;
    //print_system_state();

    //std::printf(">> Joining OS\n");
    //os_thread.join();
 
    //std::printf(">> Joining CAM\n");
    //cam_thread.join();

}

#endif
