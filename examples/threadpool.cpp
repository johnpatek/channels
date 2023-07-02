#include <channels.hpp>

#include <future>
#include <thread>
#include <vector>

#include <fstream>
#include <iostream>
#include <random>
#include <unordered_map>

class threadpool
{
private:
    channels::buffered_channel<std::function<void()>> _queue;
    std::vector<std::thread> _workers;
    bool _active;

public:
    threadpool(std::size_t worker_count = std::thread::hardware_concurrency(), std::size_t queue_size = std::thread::hardware_concurrency() * 10) : _active(true), _queue(queue_size)
    {
        _workers.reserve(worker_count);
        while (_workers.size() < _workers.capacity())
        {
            _workers.push_back(
                std::thread(
                    [&]
                    {
                        std::function<void()> work;
                        while (_queue.read(work) == channels::read_status::success)
                        {
                            work();
                        }
                    }));
        }
    }

    ~threadpool()
    {
        if (_active)
        {
            shutdown();
        }
    }

    void shutdown()
    {
        _active = false;
        _queue.close();
        for (std::thread &worker : _workers)
        {
            worker.join();
        }
    }

    template <class F, class... Args>
    auto submit(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        _queue.write(
            [task]
            {
                (*task)();
            });

        return result;
    }
};

static std::chrono::milliseconds queue_time(const std::chrono::steady_clock::time_point &start);

static void benchmark(std::size_t threads, std::size_t block_size);

int main(int argc, char **argv)
{
    for (std::size_t threads = 1; threads <= std::thread::hardware_concurrency(); threads++)
    {
        benchmark(threads, 100 * 1024);
    }
    return 0;
}

void benchmark(std::size_t threads, std::size_t block_size)
{
    threadpool scheduler(threads, 100);
    std::vector<std::future<std::chrono::milliseconds>> futures;
    std::chrono::steady_clock::time_point start;
    std::unordered_map<std::thread::id, std::ofstream> stream_map;
    std::function<std::chrono::milliseconds(const std::chrono::steady_clock::time_point &)> work_function;

    work_function = [&](const std::chrono::steady_clock::time_point &start)
    {
        const std::chrono::milliseconds latency = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        auto stream = stream_map.find(std::this_thread::get_id());
        if (stream == stream_map.end())
        {
            stream = stream_map.emplace(std::this_thread::get_id(), std::ofstream("/dev/null")).first;
        }
        for (std::size_t write_size = 0; write_size < block_size; write_size++)
        {
            stream->second.put('a');
        }
        return latency;
    };

    futures.reserve(1000);
    while (futures.size() < futures.capacity())
    {
        start = std::chrono::steady_clock::now();
        futures.push_back(scheduler.submit(work_function, start));
    }

    std::chrono::milliseconds latency(0);
    for (std::future<std::chrono::milliseconds> &future : futures)
    {
        latency += future.get();
    }
    std::cout << "avg latency(" << threads << " thread(s), " << block_size << " bytes):" << (latency / futures.size()).count() << std::endl;
}

std::chrono::milliseconds queue_time(const std::chrono::steady_clock::time_point &start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}