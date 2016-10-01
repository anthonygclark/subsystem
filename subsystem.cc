#include <memory>
#include <string>
#include <cassert>
#include <mutex>

#include "subsystem.hh"

#ifndef NDEBUG
#include <cstdio>
std::mutex debug_print_lock;

#define DEBUG_PRINT(x, ...)                                                      \
    do {                                                                         \
        std::lock_guard<decltype(debug_print_lock)> l{debug_print_lock};         \
        std::printf("\x1b[1m(%s:%d, %s)\x1b[0m\x1b[1m\x1b[34m DEBUG:\x1b[0m " x, \
                    __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__);     \
    } while(0)
#else
#define DEBUG_PRINT(x, ...) ((void)0)
#endif

namespace management
{
    namespace
    {
        constexpr const char * StateNameStrings[] = {
            [INIT]    = "INIT\0",
            [RUNNING] = "RUNNING\0",
            [STOPPED] = "STOPPED\0",
            [ERROR]   = "ERROR\0",
            [DELETE]  = "DELETE\0",
        };
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

        void SystemState::put(SystemState::key_type key, Subsystem & ss)
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


    void print_system_state(const char * caller)
    {
        auto & p = *detail::system_state.get();
        std::lock_guard<decltype(debug_print_lock)> l{debug_print_lock};

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


    void init_system_state(std::uint32_t n)
    {
        std::call_once(detail::system_state_init_flag,
                       [&n]() {
                           detail::system_state = std::make_unique<detail::SystemState>(n);
                       });
    }


    Subsystem::Subsystem(std::string const & name,
                         std::initializer_list<std::reference_wrapper<Subsystem>> && parents) :
        m_cancel_flag(),
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
        while(auto trash = m_bus.try_pop());
        stop_bus();
        commit_state(DELETE);
    }

    SubsystemTag Subsystem::generate_tag()
    {
        static std::mutex current_lock;
        static SubsystemTag current = SubsystemTag{};

        std::lock_guard<decltype(current_lock)> lock{current_lock};

        return (0x55000000 | current++);
    }

    void Subsystem::stop_bus()
    {
        m_bus.terminate();
        set_cancel_flag(true);
    }

    bool Subsystem::all_parents_running_or_cancel()
    {
        bool ret = false;

        /* in the case of no parents, this condition is true */
        if (!has_parents()) {
            ret = true;
        }
        else {
            /* if cancel flag is set, this condition is true */
            if (m_cancel_flag) {
                set_cancel_flag(false);
                ret = true;
            }
            else {
                /* go into parent map and test if each parent is running */
                ret = std::all_of(m_parents.begin(), m_parents.end(),
                                  [this] (parent_mapping_t const & p) {
                                      auto item = m_sysstate_ref.get(p);
                                      auto s = item.first;
                                      return is_in_good_state(s);
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

        DEBUG_PRINT("Associating %s subsystem with the %s subystem\n",
                    m_name.c_str(), child.get_name().c_str());

        /* lock here as this can be called from a child,
         * ie - m_parents->add_child(this)
         */
        std::unique_lock<decltype(m_state_change_mutex)> lk{m_state_change_mutex};
        m_children.insert(k);
    }

    void Subsystem::remove_child(SubsystemTag tag)
    {
        if (!m_children.count(tag))
        {
            DEBUG_PRINT("%s Subsystem does not contain child subsystem %s. Skipping\n",
                        m_name.c_str(), StateNameStrings[tag]);
            return;
        }

        std::unique_lock<decltype(m_state_change_mutex)> lk(m_state_change_mutex);
        m_children.erase(tag);
    }

    void Subsystem::add_parent(Subsystem & parent)
    {
        auto k = parent.get_tag();

        if (m_parents.count(k))
        {
            DEBUG_PRINT("%s Subsystem already has the %s Subsystem as a parent. Skipping\n",
                        m_name.c_str(), parent.get_name().c_str());
            return;
        }

        std::unique_lock<decltype(m_state_change_mutex)> lk(m_state_change_mutex);

        DEBUG_PRINT("%s: Inserting Parent 0x%08x\n", m_name.c_str(), k);
        m_parents.insert(k);
    }

    void Subsystem::remove_parent(SubsystemTag tag)
    {
        if (!m_parents.count(tag))
        {
            DEBUG_PRINT("%s Subsystem does not contain parent subsystem %s. Skipping\n",
                        m_name.c_str(), StateNameStrings[tag]);
            return;
        }

        DEBUG_PRINT("%s: Removing Parent 0x%08x\n", m_name.c_str(), tag);
        std::unique_lock<decltype(m_state_change_mutex)> lk(m_state_change_mutex);
        m_parents.erase(tag);
    }

    void Subsystem::commit_state(State new_state)
    {
        /* To account for user error */
        if (m_state == new_state) return;

        /* scope */
        {
            /* wait for a start signal */
            std::unique_lock<lock_t> lk(m_state_change_mutex);
            m_proceed_signal.wait(lk, [this] { return all_parents_running_or_cancel(); });

            /* do the actual state change */
            auto old = m_state;
            m_state = new_state;
            m_sysstate_ref.put(m_tag, m_state);

            DEBUG_PRINT("%s Subsystem changed state %s->%s\n", m_name.c_str(),
                        StateNameStrings[old], StateNameStrings[m_state]);

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

        /* nullptr signals termination */
        if (item == nullptr) return false;

        DEBUG_PRINT("%s Received %d event\n", m_name.c_str(), item->from);

        switch(item->from)
        {
        case SubsystemIPC::PARENT : handle_parent_event(*item.get()); break;
        case SubsystemIPC::CHILD  : handle_child_event(*item.get()); break;
        /* TODO exception */
        default : DEBUG_PRINT("Handling BUS message with improper `from` field\n"); break;
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
            // propgate error?
        case DELETE:
            remove_child(event.tag);
            break;
        case STOPPED:
            break;
        default:
            break;
        }

        /* hand off to the virtual handler */
        on_child(event);
    }

    void Subsystem::on_child(SubsystemIPC event)
    {
        (void)event;
        /* default implementation */
    }

    void Subsystem::handle_parent_event(SubsystemIPC event)
    {
        /* handle cancellation flag */
        switch(event.state)
        {
        case INIT:
        case RUNNING:
            // nothing here since children dont react
            // to parents starting
            break;
        default:
            set_cancel_flag(true);
            break;
        }

        /* hand off to the virtual handler */
        on_parent(event);
    }

    void Subsystem::on_parent(SubsystemIPC event)
    {
        switch(event.state)
        {
        case ERROR:
            error();
            break;
        case DELETE:
        case STOPPED:
            stop();
            break;
        case RUNNING:
            start();
            break;
        default:
            break;
        }
    }

} // end namespace management

