/**
 * @file subsystem.cc
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 */

#include <memory>
#include <string>
#include <cassert>
#include <cstdint>
#include <mutex>

#include "subsystem.hh"

#ifndef NDEBUG
#define ALLOW_DEBUG_PRINT
//#define ALLOW_DEBUG2_PRINT

#include <cstdio>
std::mutex debug_print_lock;

#if defined(ALLOW_DEBUG_PRINT)
#define DEBUG_PRINT(x, ...)                                                                                       \
    do {                                                                                                          \
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};                                         \
        std::fprintf(stderr, "\x1b[1m(%s:%d (tid:%-25zu), %-25s)\x1b[0m\x1b[1m\x1b[34m (%-15s) DEBUG:\x1b[0m " x, \
                    __FILE__, __LINE__, std::hash<std::thread::id>()(std::this_thread::get_id()),                 \
                    __func__, m_name.c_str(), ##__VA_ARGS__);                                                     \
    } while(0)
#else
#define DEBUG_PRINT(x, ...) ((void)0)
#endif

#if defined(ALLOW_DEBUG2_PRINT)
#define DEBUG_PRINT2(x, ...)                                                                                       \
    do {                                                                                                           \
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};                                          \
        std::fprintf(stderr, "\x1b[1m(%s:%d (tid:%-25zu), %-25s)\x1b[0m\x1b[1m\x1b[31m (%-15s) DEBUG: \x1b[0m " x, \
                    __FILE__, __LINE__, std::hash<std::thread::id>()(std::this_thread::get_id()),                  \
                    __func__, m_name.c_str(), ##__VA_ARGS__);                                                      \
    } while(0)
#else
#define DEBUG_PRINT2(x, ...) ((void)0)
#endif

#else
#ifndef DEBUG_PRINT
#define DEBUG_PRINT(x, ...) ((void)0)
#endif
#ifndef DEBUG_PRINT2
#define DEBUG_PRINT2(x, ...) ((void)0)
#endif
#endif

#if 0
#define D() DEBUG_PRINT2("\n")
#define DD(x) DEBUG_PRINT2("%s\n", x)
#else
#define D() ((void)0)
#define DD(x) ((void)0)
#endif

namespace management
{

    ThreadsafeQueue<std::string> & get_log_queue()
    {
        static ThreadsafeQueue<std::string> ret;
        return ret;
    }

    namespace detail
    {
        std::once_flag system_state_init_flag;

        /**< System state instance.
         * Created in init_system_state() */
        std::unique_ptr<detail::SystemState> system_state;

        state_map_t & create_or_get_state_map()
        {
            static state_map_t map;
            return map;
        }

        SystemState::SystemState(std::uint32_t max_subsystems) noexcept :
            m_max_subsystems(max_subsystems),
            map_ref(create_or_get_state_map())
        {
            map_ref.reserve(m_max_subsystems);
        }

        SystemState::~SystemState()
        {
            pthread_rwlock_destroy(&m_state_lock);
        }

        SystemState::value_type SystemState::get(SystemState::key_type key)
        {
            pthread_rwlock_rdlock(&m_state_lock);
            SystemState::value_type ret = map_ref.at(key);
            pthread_rwlock_unlock(&m_state_lock);
            return ret;
        }

        void SystemState::put(SystemState::key_type key, SystemState::value_type value)
        {
            pthread_rwlock_wrlock(&m_state_lock);
            map_ref.erase(key);
            map_ref.emplace(key, value);
            pthread_rwlock_unlock(&m_state_lock);
        }

        void SystemState::put(SystemState::key_type key, SystemState::value_type::second_type::type & ss)
        {
            assert(map_ref.size() >= m_max_subsystems && "Attempting to exceed max number of subsystems");
            auto item = get(key);
            item.second = std::ref(ss);
            put(key, std::make_pair(item.first, item.second));
        }

        void SystemState::put(SystemState::key_type key, State state)
        {
            auto item = get(key);
            put(key, std::make_pair(state, item.second));
            assert(get(key).first == state && __PRETTY_FUNCTION__);
        }

        SystemState & get_system_state()
        {
            assert(system_state && "System state not initialized. Call init_system_state(n) before starting subsystems");
            return *(system_state.get());
        }

    } /* end namespace detail */

#ifndef NDEBUG
        constexpr const char * StateNameStrings[] = {
            [INIT]    = "INIT\0",
            [RUNNING] = "RUNNING\0",
            [STOPPED] = "STOPPED\0",
            [ERROR]   = "ERROR\0",
            [DESTROY]  = "DESTROY\0",
        };

        constexpr const char * StateIPCNameStrings[] = {
            [Subsystem::SubsystemIPC::PARENT] = "PARENT\0",
            [Subsystem::SubsystemIPC::CHILD] = "CHILD\0",
            [Subsystem::SubsystemIPC::SELF] = "SELF\0"
        };

        void print_system_state()
        {
            std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};
            auto & p = *detail::system_state.get();

            for (auto & pair : p.map_ref)
                std::printf("Entry -------\n"
                            " KEY   : 0x%08x\n"
                            " STATE : %s\n"
                            "  NAME : %s\n",
                            pair.first,
                            StateNameStrings[pair.second.first],
                            pair.second.second.get().get_name().c_str());
        }
#else
        inline void print_system_state(const char * caller) { (void) caller; }
#endif

#ifndef NDEBUG
    void Subsystem::print_ipc(std::string s, Subsystem::SubsystemIPC const & ipc)
    {
        auto & p = detail::get_system_state();
        (void)p;
        (void)s;
        (void)ipc;

        DEBUG_PRINT("(%s) SubsystemIPC: from:%s, tag:%s, state:%s\n",
                    s.c_str(), StateIPCNameStrings[ipc.from],
                    p.get(ipc.tag).second.get().get_name().c_str(),
                    StateNameStrings[ipc.state]);
    }
#else
    void Subsystem::print_ipc(std::string s, Subsystem::SubsystemIPC const & ipc)
    {
        (void)s ; (void)ipc;
    }
#endif

    void init_system_state(std::uint32_t n)
    {
        std::call_once(detail::system_state_init_flag,
                       [&n]() {
                           detail::system_state = std::make_unique<detail::SystemState>(n);
                       });
    }

    Subsystem::Subsystem(std::string const & name, SubsystemParentsList parents) :
        m_cancel_flag(false),
        m_destroyed(false),
        m_name(name),
        m_state(State::INIT),
        m_sysstate_ref(detail::get_system_state())
    {
        m_tag = Subsystem::generate_tag();

        DEBUG_PRINT("Creating '%s' Subsystem with tag %08x\n",
                    m_name.c_str(), m_tag);

        /* Create a map of parents */
        for (auto & parent_item : parents)
        {
            /* add to parents */
            add_parent(parent_item.get());
            /* add this to the parent */
            parent_item.get().add_child(*this);
        }

        m_sysstate_ref.put(m_tag, {m_state, std::ref(*this)});
    }

    Subsystem::~Subsystem()
    {
    }

    SubsystemTag Subsystem::generate_tag()
    {
        static std::mutex tag_lock;
        static SubsystemTag current = SubsystemTag{};

        std::lock_guard<decltype(tag_lock)> lk{tag_lock};

        return (0x55000000 | current++);
    }

    void Subsystem::stop_bus()
    {
        while(auto trash = m_bus.try_pop()) {
            DEBUG_PRINT2("... throwing away - %s\n",
                         StateNameStrings[trash->state]);
        }

        m_bus.terminate();

        set_cancel_flag(true);
    }

    bool Subsystem::wait_for_parents()
    {
        bool ret = false;

        /* in the case of no parents, this condition is true */
        if (!has_parents()) {
            ret = true;
        }
        else if (m_state == DESTROY) {
            ret = true;
        }
        else {
            /* When the cancel flag is temporarily marked as true,
             * count this as a cancel and reset it */
            if (m_cancel_flag == true)
            {
                set_cancel_flag(false);
                ret = true;
            }
            else {
                /* go into parent map and test if each parent is running
                 * or each parent is destroyed. The running case is typical
                 * when we're waiting for parents to start. And the destroy case
                 * is typical when we're waiting for parents to shutdown/destroy
                 */
                ret = std::all_of(m_parents.begin(), m_parents.end(),
                                  [this] (parent_mapping_t const & p) {
                                      auto item = m_sysstate_ref.get(p);
                                      auto s = item.first;
                                      return (s == RUNNING || s == DESTROY);
                                  });
            }
        }

        return ret;
    }

    void Subsystem::put_message(SubsystemIPC msg)
    {
        m_bus.push(msg);
        m_proceed_signal.notify_one();
    }

    void Subsystem::add_child(Subsystem & child)
    {
        /* lock here as this can be called from a child,
         * ie - m_parents->add_child(this) */
        std::lock_guard<lock_t> lk{m_state_change_mutex};

        auto k = child.get_tag();

        /* Subsystem already contains child */
        if (m_children.count(k))
        {
            DEBUG_PRINT("Subsystem already has the %s Subsystem as a child. Skipping\n",
                        child.get_name().c_str());
            return;
        }

        DEBUG_PRINT("Inserting Child 0x%08x\n", k);

        m_children.insert(k);
    }

    void Subsystem::remove_child(SubsystemTag tag)
    {
        std::lock_guard<lock_t> lk{m_state_change_mutex};

        if (!m_children.count(tag))
        {
            DEBUG_PRINT("Subsystem does not contain child subsystem %s. Skipping\n",
                        StateNameStrings[tag]);
            return;
        }

        m_children.erase(tag);
    }

    void Subsystem::remove_parent(SubsystemTag tag)
    {
        std::lock_guard<lock_t> lk{m_state_change_mutex};

        if (!m_parents.count(tag))
        {
            DEBUG_PRINT("Subsystem does not contain parent subsystem %s. Skipping\n",
                        StateNameStrings[tag]);
            return;
        }

        m_parents.erase(tag);
    }

    void Subsystem::add_parent(Subsystem & parent)
    {
        std::lock_guard<lock_t> lk(m_state_change_mutex);

        auto k = parent.get_tag();

        if (m_parents.count(k))
        {
            DEBUG_PRINT("Subsystem already has the %s Subsystem as a parent. Skipping\n",
                        parent.get_name().c_str());
            return;
        }

        DEBUG_PRINT("Inserting Parent 0x%08x\n", k);

        m_parents.insert(k);
    }

    void Subsystem::commit_state(State new_state)
    {
        /* move to function... */
        static auto test_fn = [this] (State old_state, State new_state) -> bool {
            /* To account for user error and old messages */
            if (old_state == new_state) {
                DEBUG_PRINT("Would commit previous state (%s), skipping...\n",
                            StateNameStrings[new_state]);
                return false;
            }
            /* prevent resurrection and stale messages */
            else if (old_state == DESTROY) {
                DEBUG_PRINT("Current state is DESTROY, ignoring %s\n",
                            StateNameStrings[new_state]);
                return false;
            }

            return true;
        };

        /* test early to avoid blocking if possible */
        if (!test_fn(m_state, new_state))
            return;

        /* wait for a start signal */
        std::unique_lock<lock_t> lk{m_state_change_mutex};

        /* spurious wakeup prevention */
        while (!wait_for_parents())
            m_proceed_signal.wait(lk, [this] { return wait_for_parents(); });

        DEBUG_PRINT("Subsystem changed state %s->%s\n",
                    StateNameStrings[m_state], StateNameStrings[new_state]);

        /* do the actual state change */
        m_state = new_state;
        m_sysstate_ref.put(m_tag, m_state);

        for_all_active_parents([this] (Subsystem & p) {
                                    p.put_message({SubsystemIPC::CHILD, m_tag, m_state});
                               });

        for_all_active_children([this] (Subsystem & c) {
                                    c.put_message({SubsystemIPC::PARENT, m_tag, m_state});
                                });
    }

    bool Subsystem::handle_bus_message()
    {
        auto item = m_bus.wait_and_pop();

        /* detect termination */
        if (item == decltype(m_bus)::terminator()) {
            DEBUG_PRINT2("%s GOT BUS TERMINATOR\n", m_name.c_str());
            /* notify the last waiting state or external waiters */
            m_proceed_signal.notify_one();
            return false;
        }

        switch(item->from)
        {
        case SubsystemIPC::PARENT : handle_parent_event(*item.get()); break;
        case SubsystemIPC::CHILD  : handle_child_event(*item.get()); break;
        case SubsystemIPC::SELF   : handle_self_event(*item.get()); break;
        /* TODO exception */
        default :
            DEBUG_PRINT("Handling bus message with improper `from` field\n");
        }

        /* notify the last waiting state or external waihers */
        m_proceed_signal.notify_one();

        return true;
    }

    void Subsystem::set_cancel_flag(bool b)
    {
        m_cancel_flag = b;
    }

    void Subsystem::handle_child_event(SubsystemIPC event)
    {
        switch(event.state)
        {
        case DESTROY: remove_child(event.tag); break;
        case INIT:
        case RUNNING:
        case STOPPED:
        case ERROR: break;
        default:
            DEBUG_PRINT("Handling BUS message with improper `state` field\n");
            return;
        }

        /* hand off to the virtual handler */
        on_child(event);
    }

    void Subsystem::on_child(SubsystemIPC event) {
        /* empty default impl */
        (void)event;
    }

    void Subsystem::handle_parent_event(SubsystemIPC event)
    {
        /* handle cancellation flag */
        switch(event.state)
        {
        case INIT:
        case RUNNING:
        case ERROR: break;
        case STOPPED: break;
        case DESTROY: set_cancel_flag(true); remove_parent(event.tag); break;
        default:
            DEBUG_PRINT("Handling BUS message with improper `state` field\n");
            return;
        }

        /* hand off to the virtual handler */
        on_parent(event);
    }

    void Subsystem::handle_self_event(SubsystemIPC event)
    {
        /* handle cancellation flag */
        switch(event.state)
        {
        case RUNNING:
            on_start(); break;
        case ERROR:
            on_error(); break;
        case STOPPED:
            on_stop(); break;
        case DESTROY:
            {
                set_cancel_flag(true);
                on_destroy();
                stop_bus();
                break;
            }
        default:
            DEBUG_PRINT("Handling BUS message with improper `state` field\n");
            return;
        }

        commit_state(event.state);
    }

    void Subsystem::on_parent(SubsystemIPC event)
    {
        switch(event.state)
        {
        case ERROR: error(); break;
        case DESTROY: destroy(); break;
        case STOPPED: stop(); break;
        case RUNNING: start(); break;
        case INIT:
        default:
            break;
        }
    }

    void Subsystem::start() {
        put_message({SubsystemIPC::SELF, m_tag, RUNNING});
    }

    void Subsystem::stop() {
        put_message({SubsystemIPC::SELF, m_tag, STOPPED});
    }

    void Subsystem::error() {
        put_message({SubsystemIPC::SELF, m_tag, ERROR});
    }

    void Subsystem::destroy() {
        put_message({SubsystemIPC::SELF, m_tag, DESTROY});
    }

    ThreadedSubsystem::ThreadedSubsystem(std::string const & name, SubsystemParentsList parents) :
        Subsystem(name, parents)
    {
        m_thread = std::thread{[this] ()
        {
            while(handle_bus_message()) {
                std::this_thread::yield();
            }
        }};
    }

    ThreadedSubsystem::~ThreadedSubsystem()
    {
        m_thread.join();
        DEBUG_PRINT("Done with thread\n");
    }


} // end namespace management

