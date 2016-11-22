#ifndef _SHARED_THREADSAFE_QUEUE_H_
#define _SHARED_THREADSAFE_QUEUE_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <cstddef>

namespace management
{
    /**
     * @brief Simple Locking MPSC threadsafe queue. 
     * @detail Multiple producer, single consumer. This isn't lockfree as it adheres 
     *      to being MPSC. Searching for ARM-friendly implementations didn't yeild too
     *      many results. Since this queue is used for low throughput transports, i.e., IPC.
     *     
     *      This implementation will *own* the data given to it - as a true IPC system does.
     *
     *      This is mainly verbatim from Anthony William's 'C++ Concurrency in Action'
     *
     * @tparam T The type of data to hold
     */
    template<typename T>
        class ThreadsafeQueue final
        {
        public:
            /**< Underlaying type */
            using type = T;
            /**< Queue type */
            using data_type = std::unique_ptr<T>;
            /**< Termination type */
            using terminator = std::nullptr_t;

        private:
            /**< Underlaying queue */
            std::queue<data_type> data_queue;
            /**< Mutex (mutable since empty() is const */
            mutable std::mutex mutex;
            /**< Condition variable */
            std::condition_variable condition;

        public:
            /**
             * @brief Default constructor
             */
            ThreadsafeQueue() = default;

            /**
             * @brief Wait for poping
             * @return The value at the top of the queue
             */
            data_type wait_and_pop()
            {
                std::unique_lock<std::mutex> lk{mutex};
                condition.wait(lk, [this] { return !data_queue.empty(); });
                data_type value = std::move(data_queue.front());
                data_queue.pop();
                return value;
            }

            /**
             * @brief Pop the queue without waiting
             * @return nullptr or data_type instance
             */
            data_type try_pop()
            {
                std::lock_guard<std::mutex> lk{mutex};
                
                if (data_queue.empty()) 
                    return nullptr;
                
                data_type value = std::move(data_queue.front());
                data_queue.pop();
                return value;
            }

            /**
             * @brief Pushes a new item into the queue
             * @details Warning: The data is now owned by the queue
             * @param new_value The new queue item
             */
            void push(T new_value)
            {
                std::lock_guard<std::mutex> lk{mutex};
                /* Copy/move construct T */
                data_type data = data_type(new T(std::move(new_value)));
                
                data_queue.push(std::move(data));
                condition.notify_one();
            }

            /**
             * @brief pushes terminator type to tell any queue listeners to stop
             */
            void terminate() { push(terminator()); }

            /**
             * @return The size of the queue
             */
            int size() const 
            { 
                std::lock_guard<std::mutex> lk{mutex};
                return data_queue.size(); 
            }

        private:
            /**
             * @brief Determines in the underlying queue is empty
             * @return Empty status
             */
            bool empty() const
            {
                std::lock_guard<std::mutex> lk{mutex};
                return data_queue.empty();
            }

            /**
             * @brief Termination push
             * @param term Terminator type to tell the queue to stop
             */
            void push(terminator term)
            { 
                std::lock_guard<std::mutex> lk{mutex};
                
                /* should be convertible to our data_type */
                data_type data = term;
                data_queue.push(std::move(data));
                condition.notify_one();
            }
        };

} // end namespace managemen

#endif // guard
