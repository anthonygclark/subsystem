#ifndef _SUBSYSTEM_H_
#define _SUBSYSTEM_H_

/**
 * @file subsystem.hh
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 */

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <pthread.h>

#include "threadsafe_queue.hh"

namespace sizes
{
    constexpr const std::size_t default_max_subsystem_count = 16;
}

namespace management
{
    /* forward */
    class Subsystem;
    /* forward */
    class ThreadedSubsystem;

    enum State { INIT, RUNNING , STOPPED , ERROR , DESTROY };

    using SubsystemTag = std::uint32_t;
    using SubsystemParentsList = std::initializer_list<std::reference_wrapper<Subsystem>>;

    void init_system_state(std::uint32_t n);

#ifndef NDEBUG
    void print_system_state();
#endif

    ThreadsafeQueue<std::string> & get_log_queue();

    namespace detail
    {
        using state_map_t = std::unordered_map
            <
                SubsystemTag,
                std::pair<State, std::reference_wrapper<Subsystem>>,
                std::hash<SubsystemTag>
            >;

        /**
         * @brief Accessor for the state map
         * @return A reference to the state map
         */
        state_map_t & create_or_get_state_map();

        /**
         * @brief Basic proxy access to the shared state of all subsystems.
         * @details Having a 'global' map of subsystems complicates access, but reduces
         *          complexity of the subsystem class.
         */
        struct SubsystemMap final
        {
            /* alias */
            using key_type = state_map_t::key_type;
            using value_type = state_map_t::mapped_type;

            /**< Max number of subsystems */
            std::uint32_t m_max_subsystems;

            /**< RW lock for controlling access to the state map
             * (initialized via NSDMI). This is specific to glibc and maybe
             */
            pthread_rwlock_t m_state_lock =
                PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;

            /**< Managed state map */
            state_map_t & map_ref;

            /**
             * @brief Binding constructor
             */
            explicit SubsystemMap(std::uint32_t max_subsystems = sizes::default_max_subsystem_count) noexcept;

            /**
             * @brief Destructor
             */
            ~SubsystemMap();

            /**
             * @brief Proxy for retrieving an item from the map.
             * @details Note, this is a value type as we don't want to hold references
             * @param key The lookup
             */
             value_type get(key_type key);

            /**
             * @brief Proxy for insertion into the map via .insert
             * @param key The tag to update
             * @param value The new value
             */
            void put(key_type key, value_type value);

            /**
             * @brief Proxy for insertion into an existing entry
             * @details This updates the subsystem's pointer value
             * @param key The tag to update
             * @param item The new pointer value
             */
            void put(key_type key, value_type::second_type::type & item);

            /**
             * @brief Proxy for insertion into an existing entry
             * @details This updates the found subsystem's state
             * @param key The tag to update
             * @param state The new state
             */
            void put(key_type key, State state);
        };


        /**
         * @brief Creates and/or retrieves a reference to a global/statically allocated
         *      SubsystemMap object. TODO There should allowed to be more than one system
         *      state.
         */
        SubsystemMap & get_system_state();
    }

    /**< Alias/typedef for the systemstate bus */
    template<typename M>
        using SubsystemBus = ThreadsafeQueue<M>;

    /**
     * @brief Subsystem
     */
    class Subsystem
    {
    public:
        /**
         * @brief Simple structure containing primitives to carry state
         *   changes.
         */
        struct SubsystemIPC
        {
            enum { PARENT, CHILD, SELF } from; /**< originator */
            SubsystemTag tag; /**< The tag of the originator */
            State state; /**< The new state of the originator */
        };

    protected:
        /**< Current parent tags */
        std::set<SubsystemTag> m_parents;
        /**< Current child tags */
        std::set<SubsystemTag> m_children;
        /**< Cancellation flag, determines if a subsystem can
         * stop waiting for it's parents.
         */
        std::atomic_bool m_cancel_flag;
        /**< Temporary sentinel to determine if we need to call
         * the destroy routines while destructing. This exists since
         * some derived classes may need to explicitly call the destroy
         * routines before they themselves can be destroyed. And calling
         * the destroy routines twice might have negative effects.
         */
        bool m_destroyed;
        /**< State change lock */
        std::mutex m_state_change_mutex;
        /* alias */
        using child_mapping_t = decltype(m_children)::value_type;
        /* alias */
        using parent_mapping_t = decltype(m_parents)::value_type;
        /* alias */
        using lock_t = decltype(m_state_change_mutex);

        /**< The name of the subsystem */
        std::string m_name;
        /**< The current subsystem state */
        State m_state;
        /**< The subsystems tag */
        SubsystemTag m_tag;
        /**< The communication bus between subsystems */
        SubsystemBus<SubsystemIPC> m_bus;
        /**< The reference to the managing systemstate */
        detail::SubsystemMap & m_sysstate_ref;
        /**< State change signal */
        std::condition_variable m_proceed_signal;

    private:
        void print_ipc(std::string s, Subsystem::SubsystemIPC const & ipc);

    private:
        /**
         * @return A unique tag for this subsystem
         */
        SubsystemTag generate_tag();

        /**
         * @brief Adds a child to this subsystem
         * @param child The child pointer to add
         */
        void add_child(Subsystem & child);

        /**
         * @brief Adds a parent to this subsystem
         * @param parent The parent pointer to add
         */
        void add_parent(Subsystem & parent);

        /**
         * @brief Removes a child from this subsystem
         * @param tag The child tag to remove
         */
        void remove_child(SubsystemTag tag);

        /**
         * @brief Removes a parent from this subsystem
         * @param tag The parent tag to remove
         */
        void remove_parent(SubsystemTag tag);

        /**
         * @brief Tests if all parents are in a good state
         * @return T, If all parents are in a good state; F, otherwise
         */
        bool wait_for_parents();

        /**
         * @brief Helper to determine if the subsystem has parents.
         * @return T, has parents; F, does not have parents
         */
        bool has_parents() const {
            return m_parents.size() > 0;
        }

        /**
         * @brief Puts a message on this subsystem's message bus
         * @details Intended to be called from other subsystems as IPC
         * @param msg The state-change message to send
         */
        void put_message(SubsystemIPC msg);

        /**
         * @brief Launches a runnable for each active parent subsystem
         * @tparam Runnable The type of the runnable
         * @param runnable The runnable object
         */
        template<typename Runnable>
            void for_all_active_parents(Runnable && runnable)
            {
                for (auto & p : m_parents)
                {
                    auto target = m_sysstate_ref.get(p);
                    auto & state = target.first;
                    auto & subsys = target.second;

                    if (state == RUNNING)
                        runnable(subsys);
                }
            }

        /**
         * @brief Launches a runnable for each active child subsystem
         * @tparam Runnable The type of the runnable
       * @param runnable The runnable object
         */
        template<typename Runnable>
            void for_all_active_children(Runnable && runnable)
            {
                for (auto & c : m_children)
                {
                    auto target = m_sysstate_ref.get(c);
                    auto & state = target.first;
                    auto & subsys = target.second;

                    if (state != DESTROY)
                        runnable(subsys);
                }
            }

        /**
         * @brief Handles a single subsystem event from a child
         * @param event A by-value event.
         */
        void handle_child_event(SubsystemIPC event);

        /**
         * @brief Handles a single subsystem event from a parent
         * @param event A by-value event.
         */
        void handle_parent_event(SubsystemIPC event);

        /**
         * @brief Handles a single subsystem event from self
         * @param event A by-value event.
         */
        void handle_self_event(SubsystemIPC event);

    protected:
        /**
         * @brief Constructor
         * @param name The name of the subsystem
         * @param tag The tag of the subsystem
         * @param parents A list of parent subsystems
         */
        explicit Subsystem(std::string const & name, SubsystemParentsList parents);

        /**
         * @brief Destructor
         */
        virtual ~Subsystem();

        /**
         * @brief Custom Start function
         * @details Default Implementation
         */
        virtual void on_start() { }

        /**
         * @brief Custom Stop function
         * @details Default Implementation
         */
        virtual void on_stop() { }

        /**
         * @brief Custom Error function
         * @details Default Implementation
         */
        virtual void on_error() { }

        /**
         * @brief Custom Destroy function
         * @details Default Implementation
         */
        virtual void on_destroy() { }

        /**
         * @brief Action to take when a parent fires an event
         * @details The default implementation inherits the parent's state
         *          For example, if a parent calls error(), the child
         *          will call error. Unless you want to change this behavior,
         *          the base impl should always be called.
         * @param event The IPC message containing the info
         *          about the parent subsystem
         */
        virtual void on_parent(SubsystemIPC event);

        /**
         * @brief Action to take when a child fires an event
         * @details The default implementation does nothing
         * @param event The IPC message containing the info
         *          about the child subsystem
         */
        virtual void on_child(SubsystemIPC event);

        /**
         * @brief Sets the cancellation flag.
         * @details This bypasses any wait state the subsystem is in
         * @param b The flag value to set
         */
        void set_cancel_flag(bool b);

        /**
         * @brief Commits the state to the subsystem table
         */
        void commit_state(State state);

        /**
         * @brief Stops the event bus
         */
        void stop_bus();

    public:
        /**
         * @brief Start trigger
         */
        void start();

        /**
         * @brief Stop trigger
         */
        void stop();

        /**
         * @brief Error trigger
         */
        void error();

        /**
         * @brief Delete/Destroy trigger
         */
        void destroy();

    public:
        /**
         * @brief Handles a single bus message
         * @return T, if the message was valid; F, if /the terminator was caught
         */
        bool handle_bus_message();

        /**
         * @return The name of the subsystem
         */
        std::string const & get_name() const {
            return m_name;
        }

        /**
         * @return The subsystem tag
         */
        SubsystemTag get_tag() const {
            return m_tag;
        }

        /**
         * @return The subsystem's current state
         */
        State get_state() const {
            return m_state;
        }
    };

    /**
     * @brief Subsystem with a managed thread
     */
    class ThreadedSubsystem : public Subsystem
    {
    private:
        /**< managed thread. Must be joinable */
        std::thread m_thread;

    protected:
        /**
         * @brief Constructor
         * @param name The name of the subsystem
         * @param tag The tag of the subsystem
         * @param parents A list of parent subsystems
         */
        ThreadedSubsystem(std::string const & name, SubsystemParentsList parents);

        virtual ~ThreadedSubsystem();
    };

} // end namespace management

#endif // guard
