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

#include <future>
#include <thread>
#include <vector>

#include "channels.hpp"

TEST(channel_test, basic_test)
{
    // test data
    const std::string copy_input = "test copy";
    std::string move_input = "test move";
    // string channel
    channels::channel<std::string> channel;
    // test results
    channels::write_status write_status;
    channels::read_status read_status;
    std::string output;

    auto future = std::async(
        [&]
        {
            write_status = channel.write(copy_input);
            EXPECT_EQ(write_status, channels::write_status::success);
            write_status = channel.write(std::move(move_input));
            EXPECT_EQ(write_status, channels::write_status::success);
            channel.close();
            EXPECT_ANY_THROW(channel.write(copy_input));
        });

    read_status = channel.read(output);
    EXPECT_EQ(read_status, channels::read_status::success);
    EXPECT_EQ(output, copy_input);
    read_status = channel.read(output);
    EXPECT_EQ(read_status, channels::read_status::success);
    EXPECT_EQ(move_input, "");
    read_status = channel.read(output);
    EXPECT_EQ(read_status, channels::read_status::closed);
}

TEST(channel_test, timeout_test)
{
    // test data
    const std::string copy_input = "test copy";
    std::string move_input = "test move";
    // string channel
    channels::channel<std::string> channel;
    // test results
    channels::write_status write_status;
    channels::read_status read_status;
    std::string output;

    auto future = std::async(
        [&]
        {
            write_status = channel.write_for(copy_input, std::chrono::milliseconds(200));
            EXPECT_EQ(write_status, channels::write_status::success);
            write_status = channel.write(std::move(move_input));
            EXPECT_EQ(write_status, channels::write_status::success);
            channel.close();
            EXPECT_ANY_THROW(channel.write(copy_input));
        });

    read_status = channel.read_for(output, std::chrono::milliseconds(300));
    EXPECT_EQ(read_status, channels::read_status::success);
    EXPECT_EQ(output, copy_input);
    read_status = channel.read(output);
    EXPECT_EQ(read_status, channels::read_status::success);
    EXPECT_EQ(move_input, "");
    read_status = channel.read(output);
    EXPECT_EQ(read_status, channels::read_status::closed);
}

TEST(channel_test, deadline_test)
{
    // test data
    const std::string copy_input = "test copy";
    std::string move_input = "test move";
    // string channel
    channels::channel<std::string> channel;
    // test results
    channels::write_status write_status;
    channels::read_status read_status;
    std::string output;

    auto future = std::async(
        [&]
        {
            write_status = channel.write_until(copy_input, std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
            EXPECT_EQ(write_status, channels::write_status::success);
            write_status = channel.write(std::move(move_input));
            EXPECT_EQ(write_status, channels::write_status::success);
            channel.close();
            EXPECT_ANY_THROW(channel.write(copy_input));
        });

    read_status = channel.read_until(output, std::chrono::steady_clock::now() + std::chrono::milliseconds(300));
    EXPECT_EQ(read_status, channels::read_status::success);
    EXPECT_EQ(output, copy_input);
    read_status = channel.read(output);
    EXPECT_EQ(read_status, channels::read_status::success);
    EXPECT_EQ(move_input, "");
    read_status = channel.read(output);
    EXPECT_EQ(read_status, channels::read_status::closed);
}

TEST(buffered_channel_test, basic_test)
{
    constexpr std::size_t count = 5;
    channels::buffered_channel<std::string> channel(count);
    char fill_char = 'a';

    for (int index = 0; index < count; index++)
    {
        channel.write(std::string(10, fill_char));
    }

    std::string output;
    for (int index = 0; index < count; index++)
    {
        channel.read(output);
    }
}
