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

#include <gtest/gtest.h>

#include <cstring>

#include <future>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "channels.hpp"

// Application used to test the suitability of the channels in a larger system
namespace echo
{
    const int DEFAULT_BLOCK_SIZE = 1024;

    std::string recv_all(int fd, int block_size = DEFAULT_BLOCK_SIZE)
    {
        bool receiving(true);
        std::stringstream output;
        int recv_size;
        std::vector<char> buffer(DEFAULT_BLOCK_SIZE);
        do
        {
            recv_size = recv(fd, buffer.data(), buffer.size(), 0);
            if (recv_size > 0)
            {
                output.write(buffer.data(), recv_size);
            }
        } while (recv_size == block_size);
        return output.str();
    }

    class server
    {
    private:
        bool _active;
        int _listener;
        channels::channel<int> _readable;
        channels::buffered_channel<std::pair<int, std::string>> _response_queue;

    public:
        server(uint16_t port) : _response_queue(10)
        {
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = INADDR_ANY;
            _listener = socket(PF_INET, SOCK_STREAM, 0);
            bind(_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        }

        void close()
        {
            shutdown(_listener, SHUT_RDWR);
        }

        void listener_function()
        {
            bool listening;
            int client;

            listen(_listener, 10);

            listening = true;
            while (listening)
            {
                client = accept(_listener, NULL, NULL);
                if (client >= 0)
                {
                    _readable.write(client);
                }
                else
                {
                    listening = false;
                }
            }
            _readable.close();
        }

        void reader_function()
        {
            int readable;
            std::string request;
            while (_readable.read(readable) == channels::read_status::success)
            {
                request = recv_all(readable);
                _response_queue.write(std::make_pair(readable, request));
            }
            _response_queue.close();
        }

        void writer_function()
        {
            std::pair<int, std::string> response;
            while (_response_queue.read(response) == channels::read_status::success)
            {
                send(response.first, response.second.data(), response.second.size(), 0);
                shutdown(response.first, SHUT_RDWR);
            }
        }
    };

    class client
    {
    private:
        sockaddr_in _address;

    public:
        client(const std::string host, uint16_t port)
        {
            _address.sin_family = AF_INET;
            _address.sin_port = htons(port);
            _address.sin_addr.s_addr = inet_addr(host.c_str());
        }

        std::string echo(const std::string &message)
        {
            std::string response;
            int client = socket(PF_INET, SOCK_STREAM, 0);
            connect(
                client,
                reinterpret_cast<sockaddr *>(
                    &_address),
                sizeof(_address));
            send(client, message.data(), message.size(), 0);
            response = recv_all(client);
            close(client);
            return response;
        }
    };
}

TEST(integration_test, echo_test)
{
    // test data
    const std::string echo_input("echo string");
    std::shared_ptr<echo::server> server;
    std::shared_ptr<echo::client> client;
    std::string echo_output;

    // create echo server
    server = std::make_shared<echo::server>(12345);

    // handles adding and removing server connections
    std::thread listener_thread(
        [&]
        {
            EXPECT_NO_THROW(server->listener_function());
        });
    // handles reading requests
    std::thread reader_thread(
        [&]
        {
            EXPECT_NO_THROW(server->reader_function());
        });
    // handles writing responses
    std::thread writer_thread(
        [&]
        {
            EXPECT_NO_THROW(server->writer_function());
        });

    // create echo client
    client = std::make_shared<echo::client>("127.0.0.1", 12345);

    echo_output = client->echo(echo_input);

    EXPECT_EQ(echo_input, echo_output);

    // TODO: there should be some type of check on the shutdown
    server->close();
    
    listener_thread.join();
    reader_thread.join();
    writer_thread.join();
}
