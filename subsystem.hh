#ifndef _SUBSYSTEM_HH_3735928559_
#define _SUBSYSTEM_HH_3735928559_

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

/* Comment this out to not use/throw exceptions */
#define SUBSYSTEM_USE_EXCEPTIONS

/* Allows subsystems to transmit more than just SubsystemIPC messages
 * See SubsystemIPC_Extended
 */
#define SUBSYSTEM_HAS_BOOST

#ifdef SUBSYSTEM_USE_EXCEPTIONS
#include <stdexcept>
#endif

#ifdef SUBSYSTEM_HAS_BOOST
#include <boost/variant.hpp>
#endif

#ifndef NDEBUG
#include <iosfwd>
#endif

#include "threadsafe_queue.hh"

/**
 * @file subsystem.hh
 * @author Anthony Clark <clark.anthony.g@gmail.com>
 *
 * TODO
 * - Fix deadlock caused by not using sleeps... this is a big one folks
 * - Remove SubsystemLink
 * - Constructor (it's just gross currently)
 * - Remove dependency on threadsafe_queue
 * - Disallow cycles in parent/child mappings
 * - Move parent/child mappings to SubsystemMap/Link
 *      - Better control flow into creating dependent subsystems.
 */

namespace sizes
{
    constexpr const std::size_t default_max_subsystem_count = 16;
}

namespace management
{
    /* Forward */
    namespace detail {
        struct SubsystemLink;
    }

    /** */
    using SubsystemTag = std::uint32_t;

    /* Convenience alias */
    using SubsystemParentsList = std::initializer_list<std::reference_wrapper<detail::SubsystemLink>>;

    /**
     * \enum Subsystem state
     */
    enum class SubsystemState : std::uint8_t {
        INIT = 0, RUNNING , STOPPED , ERROR , DESTROY
    };

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

#ifdef SUBSYSTEM_HAS_BOOST
    /**
     * @brief Extended IPC type.
     * @details Allows subsystems to carry other messages aside from SubsystemIPC
     *          along their bus.
     * @tparam Ts Other types to support
     */
    template<typename... Ts>
        using SubsystemIPC_Extended = boost::variant<SubsystemIPC, Ts...>;
#endif

    namespace detail
    {
        /**
         * @brief Binding between subsystems.
         * @todo This should get reworked or removed. At least 'friend' it with
         *       SubsystemMap
         */
        struct SubsystemLink
        {
            /**< Subsystem UUID */
            SubsystemTag m_tag = 0;
            /**< Subsystem Name */
            std::string m_name = "";
            /**< Current subsystem state */
            SubsystemState m_state = SubsystemState::INIT;
            /**< Current parent tags */
            std::set<SubsystemTag> m_parents;
            /**< Current child tags */
            std::set<SubsystemTag> m_children;

            virtual ~SubsystemLink() = default;
            virtual void add_child(SubsystemLink & child) = 0;
            virtual void add_parent(SubsystemLink & parent) = 0;
            virtual void remove_child(SubsystemTag tag) = 0;
            virtual void remove_parent(SubsystemTag tag) = 0;
            virtual void put_message(SubsystemIPC msg) = 0;

            decltype(m_tag) get_tag() const { return m_tag; }
            decltype(m_name) get_name() const { return m_name; }
            decltype(m_state) get_state() const { return m_state; }
        };

    } /* end namespace detail */

    namespace helpers
    {
        /**
         * @brief Base class for dispatcher. Currently used but not needed.
         */
        struct dispatcher {
            template<typename V> bool intercept_message(V &&) { return false; }
        };

        /**
         * @brief Default ipc dispatcher
         * @details Currently unused. We might use this if we make all IPC go
         *          through CRTP to the derived. At the moment it's left here
         *          for historical reasons.
         * @tparam I CRTP dispatch target
         */
        template<typename I>
            struct ipc_dispatcher : dispatcher {
                bool intercept_message(SubsystemIPC & i) { return static_cast<I *>(this)->operator()(i); }
            };

#ifdef SUBSYSTEM_HAS_BOOST
        /**
         * @brief Boost::static_visitor interop
         * @details When a subsystem derived this class and when it received a boost::variant via
         *          it's IPC bus, this intercept_message call will be invoked, applying the visitor.
         * @tparam I CRTP dispatch target
         */
        template<typename I>
            struct extended_ipc_dispatcher : dispatcher, boost::static_visitor<bool>
        {
            template<typename V>
                bool intercept_message(V && v) {
                    return boost::apply_visitor(*static_cast<I *>(this), v);
                }
        };
#endif
    } /* end namespace helpers */

    /**
     * @brief Basic proxy access to the shared state of all subsystems.
     * @details Having a 'global' map of subsystems complicates access, but reduces
     *          complexity of the subsystem class.
     */
    class SubsystemMap final
    {
    private:
        /**< Map type that SubsystemMap manages.
         * This is tag->ref since when children are constructing, the child reaches
         * into the parent (by ref) and adds itself. This access should be better.
         */
        using SubsystemMapType = std::unordered_map<
                    SubsystemTag,
                    std::reference_wrapper<detail::SubsystemLink>, std::hash<SubsystemTag>
                >;
    public:
        /* alias */
        using key_type = SubsystemMapType::key_type;
        using value_type = SubsystemMapType::mapped_type;

    private:
        /**< Max number of subsystems */
        std::uint32_t m_max_subsystems;
        /**< Managed state map */
        SubsystemMapType m_map;
        /** RW lock */
        mutable std::mutex m_lock;

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
        void put(key_type key, value_type value);

#ifndef NDEBUG
        friend std::ostream & operator<< (std::ostream & s, SubsystemMap const & m);
#endif
    };

#ifndef NDEBUG
    constexpr const char * StateNameStrings[] = {
        "INIT\0", "RUNNING\0", "STOPPED\0",
        "ERROR\0", "DESTROY\0",
    };
#endif

    /**
     * @brief Subsystem
     * @details More docs please...
     */
    template<template <typename...> class Bus=ThreadsafeQueue, typename T = SubsystemIPC, typename Dispatch = void>
        class Subsystem : public detail::SubsystemLink
    {
    protected:
        /**< Cancellation flag, determines if a subsystem can
         * stop waiting for it's parents.
         */
        std::atomic_bool m_cancel_flag;
        /**< State change lock */
        std::mutex m_state_change_mutex;
        /* alias */
        using lock_t = decltype(m_state_change_mutex);

        /**< The communication bus between subsystems */
        Bus<T> m_bus;
        /**< The reference to the managing systemstate */
        SubsystemMap & m_subsystem_map_ref;
        /**< State change signal */
        std::condition_variable m_proceed_signal;

    private:
        /**
         * @brief Adds a child to this subsystem
         * @param child The child pointer to add
         */
        void add_child(SubsystemLink & child) override
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
        void add_parent(SubsystemLink & parent) override
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
                                      [this] (SubsystemTag const & p) {
                                          auto subsys = m_subsystem_map_ref.get(p);
                                          auto state = subsys.get().get_state();
                                          return (state != SubsystemState::INIT && state != SubsystemState::DESTROY);
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
            if (m_state == SubsystemState::DESTROY) {
#ifdef SUBSYSVTEM_USE_EXCEPTIONS
                throw std::runtime_error("Attempting to call put_message after m_state == DESTROY");
#else
                return;
#endif
            }

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
                    auto subsys = m_subsystem_map_ref.get(p);

                    if (subsys.get().get_state() == SubsystemState::RUNNING)
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
                    auto subsys = m_subsystem_map_ref.get(c);

                    if (subsys.get().get_state() != SubsystemState::DESTROY)
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
            case SubsystemState::RUNNING:
            case SubsystemState::STOPPED:
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
                    set_cancel_flag(true);
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
                    set_cancel_flag(true);
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
         * TODO COMMENT/Doc
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

            do {
                m_proceed_signal.wait(lk, [this] { return wait_for_parents(); });
                /* spurious wakeup prevention */
            } while (!wait_for_parents());

            /* do the actual state change */
            m_state = state;

            SubsystemIPC msg { SubsystemIPC::CHILD, m_tag, m_state };

            for_all_active_parents([msg] (SubsystemLink & p) {
                                      p.put_message(msg);
                                   });

            msg.from = SubsystemIPC::PARENT;

            for_all_active_children([msg] (SubsystemLink & c) {
                                      c.put_message(msg);
                                    });
        }

        /**
         * @brief Stops the event bus
         */
        void stop_bus()
        {
            while(auto trash = m_bus.try_pop()) {
                /* ignore all currently unprocessed events */
            }

            set_cancel_flag();
            m_bus.terminate();
        }

        /**
         * @brief Message handler implementation
         * @details The default implementation here does dispatch via CRTP to Dispatch. See the
         *          specialization for Subsystem<SubsystemIPC, void> below.
         * @param message The latest bus message
         * @return T if bus message was handled, F otherwise
         */
        bool handle_bus_message2(T & message)
        {
            /* compile-time check for Dispatch::intercept_message(T &)
             * I wish I could put some context message here, but the error should be enough.
             */
            constexpr bool(Dispatch::*intercept_message_caller)(T &) =
                &Dispatch::intercept_message;

            /* CRTP dispatch */
            return (static_cast<Dispatch *>(this)->*intercept_message_caller)(message);
        }

    protected:

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
         * @brief Handles a SubsystemIPC message
         * @param event The IPC message to handle
         * @return T if success, F otherwise. The F case is the exceptional path.
         */
        bool operator()(SubsystemIPC & event)
        {
            switch(event.from)
            {
            case SubsystemIPC::PARENT: handle_parent_event(event); break;
            case SubsystemIPC::CHILD: handle_child_event(event); break;
            case SubsystemIPC::SELF: handle_self_event(event); break;
            default:
#ifdef SUBSYSVTEM_USE_EXCEPTIONS
                throw std::runtime_error("Invalid from field in SubsystemIPC");
#else
                /* ignore? */
                return false;
#endif
            }

            m_proceed_signal.notify_one();
            return true;
        }

        /**
         * @brief Handles a single bus message
         * @return T, if the message was valid; F, if the terminator was caught
         */
        bool handle_bus_message()
        {
            if (m_state == SubsystemState::DESTROY) {
#ifdef SUBSYSVTEM_USE_EXCEPTIONS
                throw std::runtime_error("Attempting to handle a message after m_state == DESTROY");
#else
                return false;
#endif
            }

            auto item = m_bus.wait_and_pop();

            /* detect termination */
            if (item == typename decltype(m_bus)::terminator()) {
                /* notify the last waiting state or external waiters */
                m_proceed_signal.notify_one();
                return false;
            }

            auto message = *item.get();
            return handle_bus_message2(message);
        }

    public:
        /**
         * @brief Constructor
         * @param name The name of the subsystem
         * @param map The SubsystemMap coordinating this subsystem
         * @param parents A list of parent subsystems
         */
        Subsystem(std::string const & name,
                  SubsystemMap & map,
                  SubsystemParentsList parents={}) :
            m_cancel_flag(false),
            m_subsystem_map_ref(map)
        {
            m_tag = SubsystemMap::generate_subsystem_tag();
            m_name = name;

            /* Create a map of parents */
            for (auto & parent_item : parents) {
                /* add to parents */
                add_parent(parent_item.get());
                /* add this to the parent */
                parent_item.get().add_child(*this);
            }

            m_subsystem_map_ref.put(m_tag, std::ref<SubsystemLink>(*this));
        }

        Subsystem(Subsystem const &) = delete;

        /**
         * @brief Destructor
         */
        virtual ~Subsystem()
        {
            set_cancel_flag(true);
            m_proceed_signal.notify_all();
            m_subsystem_map_ref.remove(m_tag);
        }

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
    };

    ////// Note: specialize this for EACH  type of Bus since we can't have partial member function
    //////       specialization...

    /**
     * @brief Specialization for the default subsystem which only handles SubsystemIPC
     */
    template<>
        inline bool Subsystem<ThreadsafeQueue, SubsystemIPC, void>::handle_bus_message2(SubsystemIPC & message) {
            return operator()(message);
        }

    /**
     * @brief Subsystem with a managed thread to handle bus messages
     * @details This is useful if you want the subsystem to execute start/stop/error/destroy
     *          in its own thread. Usually this is desired.
     */
    template<template <typename...> class Bus=ThreadsafeQueue, typename T = SubsystemIPC, typename Dispatch = void>
        class ThreadedSubsystem : public Subsystem<Bus, T, Dispatch>
    {
    private:
        /**< Managed thread */
        std::thread m_thread;

    public:
        /**
         * @brief Constructor
         * @param name The name of the subsystem
         * @param map The SubsystemMap used to coordinate subsystems
         * @param parents A list of parent subsystems
         */
        ThreadedSubsystem(std::string const & name, SubsystemMap & map, SubsystemParentsList parents={}) :
            Subsystem<Bus, T, Dispatch>(name, map, parents)
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

} /* end namespace management */

#endif // guard
