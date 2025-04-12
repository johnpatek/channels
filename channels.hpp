/*
** Copyright 2023 John R. Patek Sr.
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in all
** copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
** SOFTWARE.
*/

#ifndef CHANNELS_HPP
#define CHANNELS_HPP

#include <array>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

namespace channels
{
    enum class channel_error
    {
        illegal_write,
        invalid_size,
    };

    class channel_exception : public std::exception
    {
    private:
        const std::map<channel_error, std::string> _error_messages = {
            {channel_error::illegal_write, "illegal write on closed channel"},
            {channel_error::invalid_size, "buffered channel cannot have size of zero"},
        };
        channel_error _error;
    public:
        channel_exception(channel_error error) : _error(error)
        {
        
        }

        const char * what() const noexcept override
        {
            std::string error_message("unknown error");
            std::map<channel_error, std::string>::const_iterator error_message_iterator = _error_messages.find(_error);
            if (error_message_iterator != _error_messages.end())
            {
                error_message = error_message_iterator->second;
            }
            return error_message.c_str();
        }
    };

    /** Result type for read operations.
     *  It is possible to receive a success or a closed from any read
     *  operation, while timeout can be returned from a timed operation
     *  such as read_for or read_until.
     */
    enum class read_status
    {
        success, //!< read completed without error
        timeout, //!< read did not complete before timeout or deadline
        closed,  //!< read returned no value from a closed channel
    };

    /** Result type for write operations.
     *  These values are the same as for read operations, with the only
     *  exception being that there is no closed status, as writing to a
     *  closed channel is illegal and will be treated as an error. As a
     *  result, all write operations will return success unless a timed
     *  write operation such as write_for or write_until is called, in
     *  which case a timeout is possible.
     */
    enum class write_status
    {
        success, //!< write completed without error
        timeout, //!< write did not complete before timeout or deadline
    };

    /** channel
     * @brief A channel for passing a value between threads
     *
     * @tparam Type the value type stored in this channel
     */
    template <class Type>
    class channel
    {
    private:
        enum class channel_state
        {
            writable,
            readable,
            closing,
            closed,
        };
        Type _value;
        channel_state _state;
        std::mutex _mutex;
        std::condition_variable _writable;
        std::condition_variable _readable;

        void wait(std::condition_variable &condition_variable, std::unique_lock<std::mutex> &lock, channel_state hold_state)
        {
            condition_variable.wait(
                lock,
                [&]()
                {
                    return _state != hold_state;
                });
        }

        template <class Rep, class Period = std::ratio<1>>
        bool wait_for(
            std::condition_variable &condition_variable,
            std::unique_lock<std::mutex> &lock,
            channel_state hold_state,
            const std::chrono::duration<Rep, Period> &timeout)
        {
            return condition_variable.wait_for(
                lock,
                timeout,
                [&]()
                {
                    return _state != hold_state;
                });
        }

        template <class Clock, class Duration = typename Clock::duration>
        bool wait_until(
            std::condition_variable &condition_variable,
            std::unique_lock<std::mutex> &lock,
            channel_state hold_state,
            const std::chrono::time_point<Clock, Duration> &deadline)
        {
            return condition_variable.wait_until(
                lock,
                deadline,
                [&]()
                {
                    return _state != hold_state;
                });
        }

        read_status read_channel(Type &value, std::unique_lock<std::mutex> &lock)
        {
            read_status status(read_status::success);
            switch (_state)
            {
            case channel_state::readable:
                value = _value;
                _state = channel_state::writable;
                lock.unlock();
                _writable.notify_one();
                break;
            case channel_state::closing:
                value = _value;
                _state = channel_state::closed;
                lock.unlock();
                _readable.notify_all();
                break;
            case channel_state::closed:
                status = read_status::closed;
                break;
            case channel_state::writable:
                throw channel_exception(channel_error::illegal_write);
                break;
            }
            return status;
        }

        template <class ValueType>
        write_status write_channel(ValueType value, std::unique_lock<std::mutex> &lock)
        {
            write_status status(write_status::success);
            switch (_state)
            {
            case channel_state::writable:
                if (std::is_move_assignable<ValueType>::value)
                {
                    _value = std::move(value);
                }
                else
                {
                    _value = value;
                }
                _state = channel_state::readable;
                lock.unlock();
                _readable.notify_one();
                break;
            default:
                throw channel_exception(channel_error::illegal_write);
            }
            return status;
        }

    public:
        /**
         * @brief Construct a new channel object
         *
         * When a channel object is constructed, it is in the writable state with
         * no pending read value.
         */
        channel()
        {
            _state = channel_state::writable;
        }

        /**
         * @brief Destroy the channel object
         *
         * Closes the channel if it is still open. If the channel is holding a value
         * that is not read, it will be cleaned up using its default destructor.
         *
         * @see close()
         */
        ~channel()
        {
            close();
        }

        /**
         * @brief Closes the channel
         *
         * When a channel is closed, it becomes unwritable. If the channel is closed
         * after a write, the value can still be read. The read status on a closed
         * channel will indicate whether a value was written prior to closing. Once
         * a channel has been closed, it cannot be reopened. Given that writing to a
         * closed channel is illegal, it is recommended that the writing and closing
         * of the channel are done from the same thread. For a case where multiple
         * writer threads are being used, the channel should either be used without
         * explicitly being closed, or additional synchronization measures should be
         * added to avoid race conditions.
         */
        void close()
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_state == channel_state::readable)
            {
                _state = channel_state::closing;
            }
            else if (_state == channel_state::writable)
            {
                _state = channel_state::closed;
            }
            lock.unlock();
            _writable.notify_all();
            _readable.notify_all();
        }

        /**
         * @brief reads a value from the channel
         *
         * The read operation will extract the value stored in the channel and then
         * make the channel writable again. This operation will block until either a
         * channel becomes readable or it is closed.
         *
         * @param value reference to the resulting value read from the channel
         * @see read_status
         * @return read_status returns read_status::success if a value was read from
         * the channel, or read_status::closed if the channel was closed and no value
         * was read.
         */
        read_status read(Type &value)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            wait(_readable, lock, channel_state::writable);
            return read_channel(value, lock);
        }

        /**
         * @brief reads a value from a channel within a given timeout
         * 
         * This function will is similar to read() in that it will block until the channel is
         * read or closed, but it also has the additional option of timing out after a given
         * duration. It will return immediately once a value is available or the channel is
         * closed.
         * 
         * @tparam Rep an arithmetic type representing the number of ticks
         * @tparam Period a std::ratio representing the tick period (i.e. the number of second's fractions per tick)
         * @param value reference to the resulting value read from the channel
         * @param timeout max duration the channel will wait for 
         * @return read_status returns read_status::success if a value was read from
         * the channel, read_status::closed if the channel was closed and no value
         * was read, or read_status::timeout if the timeout was exceeded.
         */
        template <class Rep, class Period = std::ratio<1>>
        read_status read_for(Type &value, const std::chrono::duration<Rep, Period> &timeout)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_for(
                       _readable,
                       lock,
                       channel_state::writable,
                       timeout)
                       ? read_channel(value, lock)
                       : read_status::timeout;
        }

        /**
         * @brief reads a value from a channel before a given deadline
         * 
         * This function will is similar to read() in that it will block until the channel is
         * read or closed, but it also has the additional option of timing out at a given time 
         * point. It will return immediately once a value is available or the channel is closed.
         * 
         * @tparam Clock the clock on which this time point is measured
         * @tparam Duration a std::chrono::duration type used to measure the time since epoch
         * @param value reference to the resulting value read from the channel
         * @param deadline max time point for the channel to wait until
         * @return read_status returns read_status::success if a value was read from
         * the channel, read_status::closed if the channel was closed and no value
         * was read, or read_status::timeout if the deadline was reached.
         */
        template <class Clock, class Duration = typename Clock::duration>
        read_status read_until(Type &value, const std::chrono::time_point<Clock, Duration> &deadline)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_until(
                       _readable,
                       lock,
                       channel_state::writable,
                       deadline)
                       ? read_channel(value, lock)
                       : read_status::timeout;
        }

        write_status write(const Type &value)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            wait(_writable, lock, channel_state::readable);
            return write_channel<const Type &>(value, lock);
        }

        write_status write(Type &&value)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            wait(_writable, lock, channel_state::readable);
            return write_channel<Type &&>(std::move(value), lock);
        }

        template <class Rep, class Period = std::ratio<1>>
        write_status write_for(const Type &value, const std::chrono::duration<Rep, Period> &timeout)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_for(
                       _writable,
                       lock,
                       channel_state::readable,
                       timeout)
                       ? write_channel<const Type &>(value, lock)
                       : write_status::timeout;
        }

        template <class Rep, class Period = std::ratio<1>>
        write_status write_for(Type &&value, const std::chrono::duration<Rep, Period> &timeout)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_for(
                       _writable,
                       lock,
                       channel_state::readable,
                       timeout)
                       ? write_channel<Type &&>(value, lock)
                       : write_status::timeout;
        }

        template <class Clock, class Duration = typename Clock::duration>
        write_status write_until(const Type &value, const std::chrono::time_point<Clock, Duration> &deadline)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_until(
                       _writable,
                       lock,
                       channel_state::readable,
                       deadline)
                       ? write_channel<const Type &>(value, lock)
                       : write_status::timeout;
        }

        template <class Clock, class Duration = typename Clock::duration>
        write_status write_until(Type &&value, const std::chrono::time_point<Clock, Duration> &deadline)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_until(
                       _writable,
                       lock,
                       channel_state::readable,
                       deadline)
                       ? write_channel<Type &&>(value, lock)
                       : write_status::timeout;
        }
    };

    template <class Type>
    class buffered_channel
    {
    private:
        std::vector<Type> _values;
        std::mutex _mutex;
        std::condition_variable _readable;
        std::condition_variable _writable;
        std::size_t _read_position;
        std::size_t _write_position;
        std::size_t _count;
        bool _open;
        

        bool is_readable() const
        {
            return !_open || (_count > 0);
        }

        bool is_writable() const
        {
            return !_open || (_count < _values.size());
        }

        template <class Predicate>
        void wait(std::condition_variable &condition_variable, std::unique_lock<std::mutex> &lock, Predicate stop_waiting)
        {
            condition_variable.wait(
                lock,
                stop_waiting);
        }

        template <class Predicate, class Rep, class Period = std::ratio<1>>
        bool wait_for(
            std::condition_variable &condition_variable,
            std::unique_lock<std::mutex> &lock,
            Predicate stop_waiting,
            const std::chrono::duration<Rep, Period> &timeout)
        {
            return condition_variable.wait_for(
                lock,
                timeout,
                stop_waiting);
        }

        template <class Predicate, class Clock, class Duration = typename Clock::duration>
        bool wait_until(
            std::condition_variable &condition_variable,
            std::unique_lock<std::mutex> &lock,
            Predicate stop_waiting,
            const std::chrono::time_point<Clock, Duration> &deadline)
        {
            return condition_variable.wait_until(
                lock,
                deadline,
                stop_waiting);
        }

        read_status read_channel(Type &value, std::unique_lock<std::mutex> &lock)
        {
            read_status status(read_status::success);
            if (!_open && (_count == 0))
            {
                status = read_status::closed;
                lock.unlock();
                _readable.notify_all();
            }
            else
            {
                value = _values[_read_position];
                _read_position = (_read_position + 1) % _values.size();
                _count--;
                lock.unlock();
                _writable.notify_one();
            }
            return status;
        }

        template <class ValueType>
        write_status write_channel(ValueType value, std::unique_lock<std::mutex> &lock)
        {
            write_status status(write_status::success);
            if (_open)
            {
                if (std::is_move_assignable<ValueType>::value)
                {
                    _values[_write_position] = std::move(value);
                }
                else
                {
                    _values[_write_position] = value;
                }
                _count++;
                _write_position = (_write_position + 1) % _values.size();
                lock.unlock();
                _readable.notify_one();
            }
            else
            {
                throw channel_exception(channel_error::illegal_write);
            }
            return status;
        }

    public:
        buffered_channel(std::size_t size) : _read_position(0), _write_position(0), _count(0), _open(true)
        {
            if (size == 0)
            {
                throw channel_exception(channel_error::invalid_size);
            }
            _values.resize(size);
        }

        ~buffered_channel()
        {
            close();
        }

        void close()
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _open = false;
            lock.unlock();
            _writable.notify_all();
            _readable.notify_all();
        }

        read_status read(Type &value)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            wait(_readable, lock, std::bind(&channels::buffered_channel<Type>::is_readable, this));
            return read_channel(value, lock);
        }

        template <class Rep, class Period = std::ratio<1>>
        read_status read_for(Type &value, const std::chrono::duration<Rep, Period> &timeout)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_for(
                       _readable,
                       lock,
                       std::bind(&channels::buffered_channel<Type>::is_readable, this),
                       timeout)
                       ? read_channel(value, lock)
                       : read_status::timeout;
        }

        template <class Clock, class Duration = typename Clock::duration>
        read_status read_until(Type &value, const std::chrono::time_point<Clock, Duration> &deadline)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_until(
                       _readable,
                       lock,
                       std::bind(&channels::buffered_channel<Type>::is_readable, this),
                       deadline)
                       ? read_channel(value, lock)
                       : read_status::timeout;
        }

        write_status write(const Type &value)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            wait(_writable, lock, std::bind(&channels::buffered_channel<Type>::is_writable, this));
            return write_channel<const Type &>(value, lock);
        }

        write_status write(Type &&value)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            wait(_writable, lock, std::bind(&channels::buffered_channel<Type>::is_writable, this));
            return write_channel<Type &&>(std::move(value), lock);
        }

        template <class Rep, class Period = std::ratio<1>>
        write_status write_for(const Type &value, const std::chrono::duration<Rep, Period> &timeout)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_for(
                       _writable,
                       lock,
                       std::bind(&channels::buffered_channel<Type>::is_writable, this),
                       timeout)
                       ? write_channel<const Type &>(value, lock)
                       : write_status::timeout;
        }

        template <class Rep, class Period = std::ratio<1>>
        write_status write_for(Type &&value, const std::chrono::duration<Rep, Period> &timeout)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_for(
                       _writable,
                       lock,
                       std::bind(&channels::buffered_channel<Type>::is_writable, this),
                       timeout)
                       ? write_channel<Type &&>(value, lock)
                       : write_status::timeout;
        }

        template <class Clock, class Duration = typename Clock::duration>
        write_status write_until(const Type &value, const std::chrono::time_point<Clock, Duration> &deadline)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_until(
                       _writable,
                       lock,
                       std::bind(&channels::buffered_channel<Type>::is_writable, this),
                       deadline)
                       ? write_channel<const Type &>(value, lock)
                       : write_status::timeout;
        }

        template <class Clock, class Duration = typename Clock::duration>
        write_status write_until(Type &&value, const std::chrono::time_point<Clock, Duration> &deadline)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            return wait_until(
                       _writable,
                       lock,
                       std::bind(&channels::buffered_channel<Type>::is_writable, this),
                       deadline)
                       ? write_channel<Type &&>(value, lock)
                       : write_status::timeout;
        }
    };
}

#endif