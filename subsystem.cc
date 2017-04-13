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

} // end namespace management

