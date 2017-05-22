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
    ThreadedSubsystem<> ss3{"ss3", m, {ss2}};

    ss1.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

#ifndef NDEBUG
    {
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};
        std::cout << std::endl << m << std::endl;
    }
#endif

    ss3.destroy();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

#ifndef NDEBUG
    {
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};
        std::cout << std::endl << m << std::endl;
    }
#endif
 
    ss1.destroy();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

#ifndef NDEBUG
    {
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};
        std::cout << std::endl << m << std::endl;
    }
#endif
}
