#ifndef _SUBSYSTEM_H_
#define _SUBSYSTEM_H_

/**
 * @file subsystem.hh
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <cassert>
#include <type_traits>

#include <pthread.h>

/* until std variant is available... */
#include <boost/variant.hpp>

#include "threadsafe_queue.hh"

/* TODO
 * - Remove SubsystemBase
 * - Constructor (it's just gross currently)
 * - When SP is defined for subsystem, ensure handle_ipc_message is defined in SP
 */

/* Comment this out to not use/throw exceptions */
#define SUBSYSTEM_USE_EXCEPTIONS

namespace sizes
{
    constexpr const std::size_t default_max_subsystem_count = 16;
}

namespace management
{
    /* Forward */
    struct SubsystemBase;

    /**
     * \enum Subsystem state
     */
    enum class SubsystemState : std::uint8_t {
        INIT = 0, RUNNING , STOPPED , ERROR , DESTROY
    };

    using SubsystemTag = std::uint32_t;

    /* Convenience alias */
    using SubsystemParentsList = std::initializer_list<std::reference_wrapper<SubsystemBase>>;

    /**< Map type that SubsystemMap manages.
     * This is tag->{state, ref} since when children are constructing, the child reaches
     * into the parent (by ref) and adds itself, for example.
     */
    using subsystem_map_type = std::unordered_map<
        SubsystemTag,
        std::pair<SubsystemState, std::reference_wrapper<SubsystemBase>>, std::hash<SubsystemTag>
    >;

    /**< Alias/typedef for the default bus */
    template<typename M>
        using DefaultSubsystemBus = ThreadsafeQueue<M>;

    /**
     * @brief Simple structure containing primitives to carry state
     *   changes.
     */
    struct SubsystemIPC
    {
        enum { PARENT, CHILD, SELF } from; /**< originator */
        SubsystemTag tag; /**< The tag of the originator */
        SubsystemState state; /**< The new state of the originator */
    };

    /**
     * @brief Basic proxy access to the shared state of all subsystems.
     * @details Having a 'global' map of subsystems complicates access, but reduces
     *          complexity of the subsystem class.
     */
    class SubsystemMap final
    {
    public:
        /* alias */
        using key_type = subsystem_map_type::key_type;
        using value_type = subsystem_map_type::mapped_type;

    private:
        /**< Max number of subsystems */
        std::uint32_t m_max_subsystems;
        /**< Managed state map */
        subsystem_map_type m_map;
        /** */
        std::mutex m_lock;
    public:
        /**
         * @return A unique tag for each subsystem
         */
        static SubsystemTag generate_subsystem_tag();

    public:
        /**
         * @brief Binding constructor
         */
        explicit SubsystemMap(std::uint32_t max_subsystems = sizes::default_max_subsystem_count) noexcept;

        /**
         * @brief Destructor
         */
        ~SubsystemMap() = default;

        /**
         * @brief Removes element
         * @param key Element to remove
         */
        void remove(key_type key);

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
        void put_new(key_type key, value_type value);

        /**
         * @brief Proxy for insertion into an existing entry
         * @details This updates the found subsystem's state
         * @param key The tag to update
         * @param state The new state
         */
        void put_state(key_type key, SubsystemState state);

#ifndef NDEBUG
        friend std::ostream & operator<< (std::ostream & s, SubsystemMap const & m);
#endif
    };

#ifndef NDEBUG
    constexpr const char * StateNameStrings[] = {
        "INIT\0",
        "RUNNING\0",
        "STOPPED\0",
        "ERROR\0",
        "DESTROY\0",
    };
#endif

    template<typename... Ts>
        using SubsystemIPC_Extended = boost::variant<SubsystemIPC, Ts...>;

    struct SubsystemBase
    {
        virtual ~SubsystemBase() = default;
        virtual void add_child(SubsystemBase & child) = 0;
        virtual void add_parent(SubsystemBase & parent) = 0;
        virtual void remove_child(SubsystemTag tag) = 0;
        virtual void remove_parent(SubsystemTag tag) = 0;
        virtual void put_message(SubsystemIPC msg) = 0;

        SubsystemTag m_tag = 0;
        std::string m_name = "";
        SubsystemState m_state = SubsystemState::INIT;

        SubsystemTag get_tag() const { return m_tag; }
        std::string get_name() const { return m_name; }
        SubsystemState get_state() const { return m_state; }
    };

    /**
     * @brief Subsystem
     * @details Communicates with other subsystems via SubsystemIPC and Bus. Bus does
     *          not have to the same on all subsystems but each Bus must support SubsystemIPC.
     *          See SubsystemIPC_Extended.
     * @tparam Bus IPC bus
     * @tparam SP Static Polymorphism type. This is a CRTP derived class that we invoke
     *          handle_ipc(Bus::type t) on. Default std::nullptr_t
     */
    template<typename Bus=DefaultSubsystemBus<SubsystemIPC>, typename SP=std::nullptr_t>
        class Subsystem : public SubsystemBase
    {
    protected:
        /**< Current parent tags */
        std::set<SubsystemTag> m_parents;
        /**< Current child tags */
        std::set<SubsystemTag> m_children;
        /**< Cancellation flag, determines if a subsystem can
         * stop waiting for it's parents.
         */
        std::atomic_bool m_cancel_flag;
        /**< State change lock */
        std::mutex m_state_change_mutex;
        /* alias */
        using child_mapping_t = typename decltype(m_children)::value_type;
        /* alias */
        using parent_mapping_t = typename decltype(m_parents)::value_type;
        /* alias */
        using lock_t = decltype(m_state_change_mutex);

        /**< The communication bus between subsystems */
        Bus m_bus;
        /**< The reference to the managing systemstate */
        SubsystemMap & m_subsystem_map_ref;
        /**< State change signal */
        std::condition_variable m_proceed_signal;

    private:
        /**
         * @brief Adds a child to this subsystem
         * @param child The child pointer to add
         */
        void add_child(SubsystemBase & child) override
        {
            /* lock here as this can be called from a child,
             * ie - m_parents->add_child(this) */
            std::lock_guard<lock_t> lk{m_state_change_mutex};
            m_children.insert(child.get_tag());
        }

        /**
         * @brief Adds a parent to this subsystem
         * @param parent The parent pointer to add
         */
        void add_parent(SubsystemBase & parent) override
        {
            std::lock_guard<lock_t> lk(m_state_change_mutex);
            m_parents.insert(parent.get_tag());
        }

        /**
         * @brief Removes a child from this subsystem
         * @param tag The child tag to remove
         */
        void remove_child(SubsystemTag tag) override
        {
            std::lock_guard<lock_t> lk{m_state_change_mutex};
            m_children.erase(tag);
        }

        /**
         * @brief Removes a parent from this subsystem
         * @param tag The parent tag to remove
         */
        void remove_parent(SubsystemTag tag) override
        {
            std::lock_guard<lock_t> lk{m_state_change_mutex};
            m_parents.erase(tag);
        }

        /**
         * @brief Tests if all parents are in a good state
         * @return T, If all parents are in a good state; F, otherwise
         */
        bool wait_for_parents()
        {
            bool ret = false;

            if (!m_parents.size()) {
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
                    ret = std::all_of(m_parents.begin(), m_parents.end(),
                                      [this] (parent_mapping_t const & p) {
                                          auto item = m_subsystem_map_ref.get(p);
                                          auto s = item.first;
                                          return (s != SubsystemState::INIT && s != SubsystemState::DESTROY);
                                      });
                }
            }

            return ret;
        }


        /**
         * @brief Puts a message on this subsystem's message bus
         * @details Intended to be called from other subsystems as IPC
         * @param msg The state-change message to send
         */
        void put_message(SubsystemIPC msg) override
        {
            m_bus.push(msg);
            m_proceed_signal.notify_one();
        }

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
                    auto target = m_subsystem_map_ref.get(p);
                    auto & state = target.first;
                    auto & subsys = target.second;

                    if (state == SubsystemState::RUNNING)
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
                    auto target = m_subsystem_map_ref.get(c);
                    auto & state = target.first;
                    auto & subsys = target.second;

                    if (state != SubsystemState::DESTROY)
                        runnable(subsys);
                }
            }

        /**
         * @brief Handles a single subsystem event from a child
         * @param event A by-value event.
         */
        void handle_child_event(SubsystemIPC event)
        {
            switch(event.state)
            {
            case SubsystemState::DESTROY:
                remove_child(event.tag);
                break;
            case SubsystemState::INIT:
                [[fallthrough]];
            case SubsystemState::RUNNING:
                [[fallthrough]];
            case SubsystemState::STOPPED:
                [[fallthrough]];
            case SubsystemState::ERROR:
                break;
            default:
#ifdef SUBSYSTEM_USE_EXCEPTIONS
                throw std::runtime_error("Invalid Child event");
#else
                return;
#endif
            }

            /* hand off to the virtual handler */
            on_child(event);
        }

        /**
         * @brief Handles a single subsystem event from a parent
         * @param event A by-value event.
         */
        void handle_parent_event(SubsystemIPC event)
        {
            /* handle cancellation flag */
            switch(event.state)
            {
            case SubsystemState::INIT: break;
            case SubsystemState::RUNNING: break;
            case SubsystemState::ERROR: break;
            case SubsystemState::STOPPED: break;
            case SubsystemState::DESTROY:
                {
                    remove_parent(event.tag);
                    set_cancel_flag();
                    break;
                }
            default:
#ifdef SUBSYSTEM_USE_EXCEPTIONS
                throw std::runtime_error("Invalid Parent event");
#else
                return;
#endif
            }

            /* hand off to the virtual handler */
            on_parent(event);
        }

        /**
         * @brief Handles a single subsystem event from self
         * @param event A by-value event.
         */
        void handle_self_event(SubsystemIPC event)
        {
            /* handle cancellation flag */
            switch(event.state)
            {
            case SubsystemState::RUNNING: on_start(); break;
            case SubsystemState::ERROR: on_error(); break;
            case SubsystemState::STOPPED: on_stop(); break;
            case SubsystemState::DESTROY:
                {
                    set_cancel_flag();
                    on_destroy();
                    stop_bus();
                    break;
                }
            default:
#ifdef SUBSYSTEM_USE_EXCEPTIONS
                throw std::runtime_error("Invalid SELF event");
#else
                return;
#endif
            }

            commit_state(event.state);
        }

        /**
         * @brief Sets the cancellation flag.
         * @details This bypasses any wait state the subsystem is in
         * @param b The flag value to set
         */
        void set_cancel_flag(bool b = true) {
            m_cancel_flag = b;
        }

        /**
         * @brief Commits the state to the subsystem table
         */
        void commit_state(SubsystemState state)
        {
            if ((m_state == state) ||
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
            m_state = state;
            m_subsystem_map_ref.put_state(m_tag, m_state);

            for_all_active_parents([this] (SubsystemBase & p) {
                                      p.put_message({SubsystemIPC::CHILD, m_tag, m_state});
                                   });

            for_all_active_children([this] (SubsystemBase & c) {
                                      c.put_message({SubsystemIPC::PARENT, m_tag, m_state});
                                    });
        }

        /**
         * @brief Stops the event bus
         */
        void stop_bus()
        {
            while(auto trash = m_bus.try_pop()) {
                /* ignore things here */
            }

            m_bus.terminate();
            set_cancel_flag();
        }

    protected:
        /**
         * @brief Constructor
         * @param name The name of the subsystem
         * @param map The SubsystemMap coordinating this subsystem
         * @param parents A list of parent subsystems
         */
        Subsystem(std::string const & name,
                  SubsystemMap & map,
                  SubsystemParentsList parents) :
            m_cancel_flag(false),
            m_subsystem_map_ref(map)
        {
            m_tag = SubsystemMap::generate_subsystem_tag();
            m_name = name;

            /* Create a map of parents */
            for (auto & parent_item : parents)
            {
                /* add to parents */
                add_parent(parent_item.get());
                /* add this to the parent */
                parent_item.get().add_child(*this);
            }

            m_subsystem_map_ref.put_new(m_tag,
                                        {m_state, std::ref<SubsystemBase>(*this)});
        }

        Subsystem(Subsystem const &) = delete;

        /**
         * @brief Destructor
         */
        virtual ~Subsystem()
        {
            m_subsystem_map_ref.remove(m_tag);
        }

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
        virtual void on_parent(SubsystemIPC event)
        {
            switch(event.state)
            {
            case SubsystemState::ERROR: error(); break;
            case SubsystemState::DESTROY: destroy(); break;
            case SubsystemState::STOPPED: stop(); break;
            case SubsystemState::RUNNING: start(); break;
            case SubsystemState::INIT: break;
            default:
#ifdef SUBSYSTEM_USE_EXCEPTIONS
                throw std::runtime_error("Invalid state field in SubsystemIPC");
#else
                /* ignore? */
#endif
                break;
            }
        }

        /**
         * @brief Action to take when a child fires an event
         * @details The default implementation does nothing
         * @param event The IPC message containing the info
         *          about the child subsystem
         */
        virtual void on_child(SubsystemIPC event) {
            /* empty default impl */
            (void)event;
        }

        /**
         * @brief
         * @param msg
         */
        bool put_message_extended(typename Bus::type msg)
        {
            if constexpr (std::is_same<typename Bus::type, SubsystemIPC>::value)
                return put_message(msg);
            else if constexpr (std::is_same<SP, std::nullptr_t>::value == false) {
                m_bus.push(msg);
                m_proceed_signal.notify_one();
                return true;
            }

            throw std::runtime_error("Unhandled put_message_extended() path");
        }

        /**
         * @brief Handle IPC message dispatch
         * @detail If SP is not nullptr_t and Bus::type is not SubsystemIPC, then
         *          this will call upward into SP's handle_ipc_message_call.
         * @param event The event to handle
         * @return T if success, F otherwise
         */
        bool handle_ipc_message(typename Bus::type event)
        {
            if constexpr (std::is_same<typename Bus::type, SubsystemIPC>::value)
                return handle_subsystem_ipc_message(event);
            else if constexpr (std::is_same<SP, std::nullptr_t>::value == false)
                return static_cast<SP*>(this)->handle_ipc_message(event);

            throw std::runtime_error("Unhandled handle_ipc() path");
        }

        /**
         * @brief Handles a SubsystemIPC message (and only that)
         * @param event The IPC message to handle
         * @return T if success, F otherwise. The F case is the exceptional path.
         */
        bool handle_subsystem_ipc_message(SubsystemIPC event)
        {
            switch(event.from)
            {
            case SubsystemIPC::PARENT: handle_parent_event(event); break;
            case SubsystemIPC::CHILD: handle_child_event(event); break;
            case SubsystemIPC::SELF: handle_self_event(event); break;
            default:
#ifdef SUBSYSTEM_USE_EXCEPTIONS
                throw std::runtime_error("Invalid from field in SubsystemIPC");
#else
                /* ignore? */
                return false;
#endif
            }

            m_proceed_signal.notify_one();
            return true;
        }

    public:
        /**
         * @brief Start trigger
         */
        void start() {
            put_message({SubsystemIPC::SELF, m_tag, SubsystemState::RUNNING});
        }

        /**
         * @brief Stop trigger
         */
        void stop() {
            put_message({SubsystemIPC::SELF, m_tag, SubsystemState::STOPPED});
        }

        /**
         * @brief Error trigger
         */
        void error() {
            put_message({SubsystemIPC::SELF, m_tag, SubsystemState::ERROR});
        }

        /**
         * @brief Delete/Destroy trigger
         */
        void destroy() {
            put_message({SubsystemIPC::SELF, m_tag, SubsystemState::DESTROY});
        }

    public:
        /**
         * @brief Handles a single bus message
         * @return T, if the message was valid; F, if /the terminator was caught
         */
        bool handle_bus_message()
        {
            auto item = m_bus.wait_and_pop();

            /* detect termination */
            if (item == typename Bus::terminator()) {
                /* notify the last waiting state or external waiters */
                m_proceed_signal.notify_one();
                return false;
            }

            /* Retreive value from unique_ptr in ThreadedSubsystem.
             * TODO this depends on ThreadedSubsystem or its interface
             */
            return handle_ipc_message(*item.get());
        }

    };

    /**
     * @brief Subsystem with a managed thread to handle bus messages
     * @details This is useful if you want the subsystem to execute start/stop/error/destroy
     *          in its own thread. Usually this is desired.
     */
    template<typename... Ts>
        class ThreadedSubsystem : public Subsystem<Ts...>
    {
    private:
        /**< managed thread. Must be joinable */
        std::thread m_thread;

    protected:
        /**
         * @brief Constructor
         * @param name The name of the subsystem
         * @param map The SubsystemMap used to coordinate subsystems
         * @param parents A list of parent subsystems
         */
        ThreadedSubsystem(std::string const & name, SubsystemMap & map, SubsystemParentsList parents) :
            Subsystem<Ts...>(name, map, parents)
        {
            m_thread = std::thread{[this] ()
                {
                    while(this->handle_bus_message()) {
                        std::this_thread::yield();
                    }
                }
            };
        }

        virtual ~ThreadedSubsystem()
        {
            if (m_thread.joinable())
                m_thread.join();
        }
    };

    using DefaultThreadedSubsystem = ThreadedSubsystem<DefaultSubsystemBus<SubsystemIPC>>;

} // end namespace management

#endif // guard
