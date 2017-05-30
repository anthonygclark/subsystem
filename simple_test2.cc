#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>

#include "subsystem.hh"

std::mutex debug_print_lock;

using namespace management;

int main()
{
    SubsystemMap m{};
    ThreadedSubsystem<> ss1{"ss1", m};
    ThreadedSubsystem<> ss2{"ss2", m, {ss1}};

    ss1.start();
    
    /* TODO remove sleep to cause deadlock :( */
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    ss1.destroy();
}
