#include <iostream>
#include <functional>
#include <csignal>
#include <memory>
#include <channels.hpp>

static std::shared_ptr<channels::channel<int>> channel;

static void signal_handler(int signal)
{
    channel->write(signal);
}

int main(int argc, char **argv)
{
    int signal;

    channel = std::make_shared<channels::channel<int>>();

    // register interrupt handler
    std::signal(SIGINT, signal_handler);

    std::cout << "Paused. Press Ctrl+C to exit." << std::endl;

    // main will block here until signal is received
    channel->read(signal);

    std::cout << std::endl
              << "received interrupt signal" << std::endl;

    return 0;
}
