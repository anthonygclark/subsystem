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
#define ALLOW_DEBUG2_PRINT

#include <cstdio>
std::mutex debug_print_lock;

#if defined(ALLOW_DEBUG_PRINT)
#define DEBUG_PRINT(x, ...)                                                                       \
    do {                                                                                          \
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};                         \
        std::printf("\x1b[1m(%s:%d (tid:%-25zu), %-25s)\x1b[0m\x1b[1m\x1b[34m DEBUG:\x1b[0m " x,  \
                    __FILE__, __LINE__, std::hash<std::thread::id>()(std::this_thread::get_id()), \
                    __func__, ##__VA_ARGS__);                                                     \
    } while(0)
#else
#define DEBUG_PRINT(x, ...) ((void)0)
#endif

#if defined(ALLOW_DEBUG2_PRINT)
#define DEBUG_PRINT2(x, ...)                                                                             \
    do {                                                                                                 \
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};                                \
        std::printf("\x1b[1m(%s:%d (tid:%-25zu), %-25s)\x1b[0m\x1b[1m\x1b[31m DEBUG: (ss:%s)\x1b[0m " x, \
                    __FILE__, __LINE__, std::hash<std::thread::id>()(std::this_thread::get_id()),        \
                    __func__, m_name.c_str(), ##__VA_ARGS__);                                            \
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

namespace management
{
    namespace
    {
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
#endif
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

    } // end namespace detail

#ifndef NDEBUG
    void print_system_state(const char * caller)
    {
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};
        auto & p = *detail::system_state.get();

        if (caller)
            std::printf("~~~~~~ CALLER : %s ~~~~~~\n", caller);

        for (auto & pair : p.map_ref)
            std::printf("Entry -------\n"
                       " KEY   : 0x%08x\n"
                       " STATE : %s\n"
                       "  NAME : %s\n",
                       pair.first,
                       StateNameStrings[pair.second.first],
                       pair.second.second.get().get_name().c_str());
    }

    void print_ipc(std::string & s, Subsystem::SubsystemIPC const & ipc)
    {
        auto & p = detail::get_system_state();

        DEBUG_PRINT("(%s) SubsystemIPC: from:%s, tag:%s, state:%s\n",
                    s.c_str(), StateIPCNameStrings[ipc.from],
                    p.get(ipc.tag).second.get().get_name().c_str(),
                    StateNameStrings[ipc.state]);
    }
#else
    void print_system_state(const char * caller) { (void) caller; }
    void print_ipc(std::string & s, Subsystem::SubsystemIPC const & ipc) { (void)s ; (void)ipc; }
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

        /* Create a map of parents */
        for (auto parent_item : parents)
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
        if (!m_destroyed)
            destroy_now();
    }

    SubsystemTag Subsystem::generate_tag()
    {
        static std::mutex current_lock;
        static SubsystemTag current = SubsystemTag{};

        std::lock_guard<decltype(current_lock)> lk{current_lock};

        return (0x55000000 | current++);
    }

    void Subsystem::stop_bus()
    {
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
    }

    void Subsystem::add_child(Subsystem & child)
    {
        auto k = child.get_tag();

        /* Subsystem already contains child */
        if (m_children.count(k))
        {
            DEBUG_PRINT("%s Subsystem already has the %s Subsystem as a child. Skipping\n",
                        m_name.c_str(), child.get_name().c_str());
            return;
        }

        DEBUG_PRINT("Associating %s subsystem with the %s subsystem\n",
                    m_name.c_str(), child.get_name().c_str());

        /* lock here as this can be called from a child,
         * ie - m_parents->add_child(this) */
        std::lock_guard<lock_t> lk{m_state_change_mutex};
        m_children.insert(k);
    }

    void Subsystem::remove_child(SubsystemTag tag)
    {
        std::lock_guard<lock_t> lk{m_state_change_mutex};

        if (!m_children.count(tag))
        {
            DEBUG_PRINT("%s Subsystem does not contain child subsystem %s. Skipping\n",
                        m_name.c_str(), StateNameStrings[tag]);
            return;
        }

        m_children.erase(tag);
    }

    void Subsystem::add_parent(Subsystem & parent)
    {
        std::lock_guard<lock_t> lk(m_state_change_mutex);

        auto k = parent.get_tag();

        if (m_parents.count(k))
        {
            DEBUG_PRINT("%s Subsystem already has the %s Subsystem as a parent. Skipping\n",
                        m_name.c_str(), parent.get_name().c_str());
            return;
        }

        DEBUG_PRINT("%s: Inserting Parent 0x%08x\n", m_name.c_str(), k);

        m_parents.insert(k);
    }

    void Subsystem::commit_state(State new_state)
    {
        /* move to function... */
        auto test_fn = [=] () -> bool {
            /* To account for user error and old messages */
            if (m_state == new_state) {
                DEBUG_PRINT("Would commit previous state (%s), skipping...\n",
                            StateNameStrings[new_state]);
                return false;
            }
            /* prevent resurrection and stale messages */
            else if (m_state == DESTROY) {
                DEBUG_PRINT("Current state is DESTROY, ignoring %s\n",
                            StateNameStrings[new_state]);
                return false;
            }

            return true;
        };

        /* test early to avoid blocking if possible */
        if (!test_fn()) return;

        /* scope */
        {
            /* wait for a start signal */
            std::unique_lock<lock_t> lk{m_state_change_mutex};
            m_proceed_signal.wait(lk, [this] { return wait_for_parents(); });

            /* test for spurious messages unordered messages */
            if (!test_fn()) return;

            DEBUG_PRINT("%s Subsystem changed state %s->%s\n", m_name.c_str(),
                        StateNameStrings[m_state], StateNameStrings[new_state]);

            /* do the actual state change */
            m_state = new_state;
            m_sysstate_ref.put(m_tag, m_state);

            DEBUG_PRINT("Firing to %zu parents and %zu children\n",
                        m_parents.size(), m_children.size());

            for_all_active_parents([this]  (Subsystem & p) {
                                        p.put_message({SubsystemIPC::CHILD, m_tag, m_state});
                                   });

            for_all_active_children([this] (Subsystem & c) {
                                        c.put_message({SubsystemIPC::PARENT, m_tag, m_state});
                                    });
        }
    }

    bool Subsystem::handle_bus_message()
    {
        auto item = m_bus.wait_and_pop();

        /* detect termination */
        if (item == decltype(m_bus)::terminator())
            return false;

        DEBUG_PRINT("(%s) SubsystemIPC: from:%s, tag:%s, state:%s\n",
                    m_name.c_str(), StateIPCNameStrings[item->from],
                    m_sysstate_ref.get(item->tag).second.get().get_name().c_str(),
                    StateNameStrings[item->state]);

        switch(item->from)
        {
        case SubsystemIPC::PARENT : handle_parent_event(*item.get()); break;
        case SubsystemIPC::CHILD  : handle_child_event(*item.get()); break;
        case SubsystemIPC::SELF   : handle_self_event(*item.get()); break;
        /* TODO exception */
        default : DEBUG_PRINT("Handling bus message with improper `from` field\n"); break;
        }

        /* notify the last waiting state or external waiters */
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
        case ERROR:
            // propagate error?
        case DESTROY: remove_child(event.tag); break;
        case INIT:
        case RUNNING:
        case STOPPED:
            break;
        default:
            DEBUG_PRINT("Handling BUS message with improper `state` field\n");
            return;
        }

        /* hand off to the virtual handler */
        on_child(event);
    }

    void Subsystem::on_child(SubsystemIPC event) {
        (void)event;
    }

    void Subsystem::handle_parent_event(SubsystemIPC event)
    {
        /* handle cancellation flag */
        switch(event.state)
        {
        case INIT:
        case RUNNING:
            /* Nothing here since children do not react
             * to parents starting
             */
            break;
        case ERROR:
        case DESTROY: set_cancel_flag(true); break;
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
        case RUNNING: on_start(); break;
        case ERROR: on_error(); break;
        case STOPPED:
            on_stop();
            set_cancel_flag(true);
            break;
        case DESTROY:
            on_destroy();
            set_cancel_flag(true);
            break;
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
        case DESTROY:
        case STOPPED: stop(); break;
        case RUNNING: start(); break;
        case INIT:
        default:
            break;
        }
    }

    void Subsystem::start() {
        //on_start();
        //commit_state(RUNNING);
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

    void Subsystem::destroy_now() {
        commit_state(DESTROY);
        stop_bus();
        m_destroyed = true;
    }

    ThreadedSubsystem::ThreadedSubsystem(std::string const & name, SubsystemParentsList parents) :
        Subsystem(name, parents)
    {
        m_thread = std::thread{[this] () {
            while(handle_bus_message());
        }};
    }

    ThreadedSubsystem::~ThreadedSubsystem() {
        destroy_now();
        m_thread.join();
    }

} // end namespace management

