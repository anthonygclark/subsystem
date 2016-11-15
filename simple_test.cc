#include <cstdio>
#include "subsystem.hh"

using namespace management;

struct FirstParent : ThreadedSubsystem
{
public:
  FirstParent() :
      ThreadedSubsystem("FirstParent", {} /* no parents */)
  { }
  /* no overridden impls */
};

struct FirstChild : ThreadedSubsystem
{
public:
  FirstChild(SubsystemParentsList parents) :
      ThreadedSubsystem("FirstChild", parents)
  {
  	/* init members */
  }
  
  void on_start() override {
	/* start members. If members are started at 
	 * init time, then maybe implement a .start() and
	 * .stop() for reactive members.
	 */
  }
  
  void on_error() override {
    std::printf("ERROR CASE!\n");
    /* maybe do this... */
    on_stop();
  }
 
  void on_stop() override {
    /* put members in a stop state, nothing should
     * be destroyed yet, just waiting */
  }
};

int main(void)
{
  init_system_state(2);
  FirstParent parent{};
  FirstChild child{parent};
  
  parent.start(); 
  /* triggers parent.on_start() then child.on_start() */
#define SIM_MS(X) std::this_thread::sleep_for(std::chrono::milliseconds(X))
#define SIM_S(X) std::this_thread::sleep_for(std::chrono::seconds(X))

  parent.error();
  /* triggers parent.on_error(), then child.on_error() */
  
  parent.destroy();
}

