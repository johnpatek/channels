# Channels Documentation

C++ synchronization framework inspired by channels from golang.

## Requirements

There are very basic requirements for using channels:
+ C++14 or later
+ A CPU with at least 2 threads

## Basic Usage

The simplest example is passing a single value from one thread to another.

```cpp
#include <chrono>
#include <iostream>
#include <thread>

#include <channels.hpp>

int main()
{
    channels::channel<std::pair<std::thread::id, int>> channel;

    std::thread sender(
        [&]
        {
            // wait 3 seconds
            std::this_thread::sleep_for(std::chrono::seconds(3));
            // send value to channel
            channel.write(std::make_pair(std::this_thread::get_id(), 0));
        });

    std::thread receiver(
        [&]
        {
            std::pair<std::thread::id, int> value;
            // this thread will block here until a value is received
            channel.read(value);
            // print receiver thread, value, and sender thread
            std::cout << "thread " << std::this_thread::get_id()
                      << " received value " << value.second
                      << " from thread " << value.first << std::endl;
        });

    sender.join();
    receiver.join();
    return 0;
}
```

The output will resemble something like the following:
```bash
thread 140077967599296 received value 0 from thread 140077975992000
```

## Tips and Pitfalls

Like golang, channels can be read from even after they have been closed, provided that
there has been a write before closing. It is also like golang in that it is illegal to
write to a closed channel. The easiest way to avoid this problem is to either let the
channel close itself in the destructor, or to use write and close the channel from the
same thread.

All operations are currently blocking. Future versions may include additional non-blocking 
operations, but this is not currently on the roadmap. This means any operation will block 
indefinitely until a status can be returned from a corresponding operations in another 
thread.