#include <cassert>
#include <cstdint>
#include <mutex>

#ifndef NDEBUG
#include <iostream>
#endif

#include "subsystem.hh"

/**
 * @file subsystem.cc
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 */

namespace management
{
    SubsystemMap::SubsystemMap(std::uint32_t max_subsystems) noexcept :
        m_max_subsystems(max_subsystems)
    {
        m_map = SubsystemMapType{};
        m_map.reserve(m_max_subsystems);
    }

    SubsystemTag SubsystemMap::generate_subsystem_tag()
    {
        static std::mutex tag_lock;
        static SubsystemTag current = SubsystemTag{};

        std::lock_guard<decltype(tag_lock)> lk{tag_lock};

        return (0x55000000 | ++current);
    }

    void SubsystemMap::remove(SubsystemMap::key_type key)
    {
        std::lock_guard<decltype(m_lock)> lk{m_lock};
        /* explicitly ignore return */
        (void)m_map.erase(key);
    }

    SubsystemMap::value_type SubsystemMap::get(SubsystemMap::key_type key)
    {
        std::lock_guard<decltype(m_lock)> lk{m_lock};
        SubsystemMap::value_type ret = m_map.at(key);
        return ret;
    }

    void SubsystemMap::put_new(SubsystemMap::key_type key, SubsystemMap::value_type value)
    {
        std::lock_guard<decltype(m_lock)> lk{m_lock};
        m_map.erase(key);
        m_map.emplace(key, value);
    }

    void SubsystemMap::put_state(SubsystemMap::key_type key, SubsystemState state)
    {
        std::lock_guard<decltype(m_lock)> lk{m_lock};

        auto item = m_map.at(key);
        m_map.erase(key);
        m_map.emplace(key, std::make_pair(state, item.second));

        assert(m_map.at(key).first == state);
    }

#ifndef NDEBUG
    std::ostream & operator<< (std::ostream & str, SubsystemMap const & m)
    {
        std::lock_guard<decltype(SubsystemMap::m_lock)> lk{m.m_lock};

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

