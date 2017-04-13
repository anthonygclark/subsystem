/**
 * @file subsystem.cc
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 */

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>

#ifndef NDEBUG
#include <iostream>
#endif

#include "subsystem.hh"

namespace management
{
    SubsystemMap::SubsystemMap(std::uint32_t max_subsystems) noexcept :
        m_max_subsystems(max_subsystems)
    {
        m_map = subsystem_map_type{};
        m_map.reserve(m_max_subsystems);
    }

    SubsystemMap::~SubsystemMap()
    {
        pthread_rwlock_destroy(&m_state_lock);
    }

    SubsystemMap::value_type SubsystemMap::get(SubsystemMap::key_type key)
    {
        pthread_rwlock_rdlock(&m_state_lock);
        SubsystemMap::value_type ret = m_map.at(key);
        pthread_rwlock_unlock(&m_state_lock);
        return ret;
    }

    void SubsystemMap::put(SubsystemMap::key_type key, SubsystemMap::value_type value)
    {
        pthread_rwlock_wrlock(&m_state_lock);
        m_map.erase(key);
        m_map.emplace(key, value);
        pthread_rwlock_unlock(&m_state_lock);
    }

    void SubsystemMap::put(SubsystemMap::key_type key, SubsystemMap::value_type::second_type::type & ss)
    {
        assert(m_map.size() >= m_max_subsystems && "Attempting to exceed max number of subsystems");
        auto item = get(key);
        item.second = std::ref(ss);
        put(key, std::make_pair(item.first, item.second));
    }

    void SubsystemMap::put(SubsystemMap::key_type key, SubsystemState state)
    {
        auto item = get(key);
        put(key, std::make_pair(state, item.second));
        assert(get(key).first == state);
    }

#ifndef NDEBUG
    std::mutex debug_print_lock;

    constexpr const char * StateNameStrings[] = {
        "INIT\0",
        "RUNNING\0",
        "STOPPED\0",
        "ERROR\0",
        "DESTROY\0",
    };

    std::ostream & operator<< (std::ostream & str, SubsystemMap const & m)
    {
        std::lock_guard<decltype(debug_print_lock)> lk{debug_print_lock};

        for (auto & pair : m.m_map)
        {
            str << "SubsystemMap Entry -------\n"
                << " KEY   : " << std::to_string(pair.first) << std::endl
                << " STATE : " << StateNameStrings[static_cast<int>(pair.second.first)] << std::endl
                << "  NAME : " << pair.second.second.get().get_name().c_str() << std::endl;
        }

        return str;
    }
#endif

    Subsystem::Subsystem(std::string const & name, SubsystemMap & map, SubsystemParentsList parents) :
        m_cancel_flag(false),
        m_name(name),
        m_state(SubsystemState::INIT),
        m_subsystem_map_ref(map)
    {
        m_tag = Subsystem::generate_tag();

        /* Create a map of parents */
        for (auto & parent_item : parents)
        {
            /* add to parents */
            add_parent(parent_item.get());
            /* add this to the parent */
            parent_item.get().add_child(*this);
        }

        m_subsystem_map_ref.put(m_tag, {m_state, std::ref(*this)});
    }

    SubsystemTag Subsystem::generate_tag()
    {
        static std::mutex tag_lock;
        static SubsystemTag current = SubsystemTag{};

        std::lock_guard<decltype(tag_lock)> lk{tag_lock};

        return (0x55000000 | ++current);
    }

    void Subsystem::stop_bus()
    {
        while(auto trash = m_bus.try_pop()) {
            /* ignore things here */
        }

        m_bus.terminate();

        set_cancel_flag(true);
    }

    bool Subsystem::wait_for_parents()
    {
        bool ret = false;

        if (!has_parents()) {
            ret = true;
        }
        else if (m_state == SubsystemState::DESTROY) {
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
                                      auto item = m_subsystem_map_ref.get(p);
                                      auto s = item.first;
                                      return (s == SubsystemState::RUNNING || s == SubsystemState::DESTROY);
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
        if (m_children.count(k)) {
            return;
        }

        m_children.insert(k);
    }

    void Subsystem::remove_child(SubsystemTag tag)
    {
        std::lock_guard<lock_t> lk{m_state_change_mutex};

        if (!m_children.count(tag)) {
            return;
        }

        m_children.erase(tag);
    }

    void Subsystem::remove_parent(SubsystemTag tag)
    {
        std::lock_guard<lock_t> lk{m_state_change_mutex};

        if (!m_parents.count(tag)) {
            return;
        }

        m_parents.erase(tag);
    }

    void Subsystem::add_parent(Subsystem & parent)
    {
        std::lock_guard<lock_t> lk(m_state_change_mutex);

        auto k = parent.get_tag();

        if (m_parents.count(k)) {
            return;
        }

        m_parents.insert(k);
    }

    void Subsystem::commit_state(SubsystemState new_state)
    {
        if ((m_state == new_state) ||
            (m_state == SubsystemState::DESTROY))
        {
            return;
        }

        /* wait for a start signal */
        std::unique_lock<lock_t> lk{m_state_change_mutex};

        /* spurious wakeup prevention */
        while (!wait_for_parents()) {
            m_proceed_signal.wait(lk, [this] { return wait_for_parents(); });
        }

        /* do the actual state change */
        m_state = new_state;
        m_subsystem_map_ref.put(m_tag, m_state);

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
            /* notify the last waiting state or external waiters */
            m_proceed_signal.notify_one();
            return false;
        }

        switch(item->from)
        {
        case SubsystemIPC::PARENT: handle_parent_event(*item.get()); break;
        case SubsystemIPC::CHILD: handle_child_event(*item.get()); break;
        case SubsystemIPC::SELF: handle_self_event(*item.get()); break;
        /* TODO exception */
        default :
            return true;
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
        case SubsystemState::DESTROY: remove_child(event.tag); break;
        case SubsystemState::INIT:
        case SubsystemState::RUNNING:
        case SubsystemState::STOPPED:
        case SubsystemState::ERROR: break;
        default:
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
        case SubsystemState::INIT:
        case SubsystemState::RUNNING:
        case SubsystemState::ERROR: break;
        case SubsystemState::STOPPED: break;
        case SubsystemState::DESTROY: set_cancel_flag(true); remove_parent(event.tag); break;
        default:
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
        case SubsystemState::RUNNING:
            on_start(); break;
        case SubsystemState::ERROR:
            on_error(); break;
        case SubsystemState::STOPPED:
            on_stop(); break;
        case SubsystemState::DESTROY:
            {
                set_cancel_flag(true);
                on_destroy();
                stop_bus();
                break;
            }
        default:
            return;
        }

        commit_state(event.state);
    }

    void Subsystem::on_parent(SubsystemIPC event)
    {
        switch(event.state)
        {
        case SubsystemState::ERROR: error(); break;
        case SubsystemState::DESTROY: destroy(); break;
        case SubsystemState::STOPPED: stop(); break;
        case SubsystemState::RUNNING: start(); break;
        case SubsystemState::INIT:
        default:
            break;
        }
    }

    void Subsystem::start() {
        put_message({SubsystemIPC::SELF, m_tag, SubsystemState::RUNNING});
    }

    void Subsystem::stop() {
        put_message({SubsystemIPC::SELF, m_tag, SubsystemState::STOPPED});
    }

    void Subsystem::error() {
        put_message({SubsystemIPC::SELF, m_tag, SubsystemState::ERROR});
    }

    void Subsystem::destroy() {
        put_message({SubsystemIPC::SELF, m_tag, SubsystemState::DESTROY});
    }

    ThreadedSubsystem::ThreadedSubsystem(std::string const & name, SubsystemMap & map, SubsystemParentsList parents) :
        Subsystem(name, map, parents)
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
    }

} // end namespace management

