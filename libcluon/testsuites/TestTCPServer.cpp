/*
 * Copyright (C) 2018  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "catch.hpp"

#include "cluon/TCPServer.hpp"
#include "cluon/TCPConnection.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

TEST_CASE("Creating TCPServer and stop immediately.") {
    cluon::TCPServer srv1{1234, nullptr};
    REQUIRE(srv1.isRunning());
}

TEST_CASE("Trying to receive with wrong sendToPort.") {
    cluon::TCPServer failingSrv2{0, nullptr};
    REQUIRE(!failingSrv2.isRunning());
}

TEST_CASE("Creating TCPServer and receive data from one connection.") {
    auto before = std::chrono::system_clock::now();

    // Setup data structures to receive data from UDPReceiver.
    std::atomic<bool> hasDataReceived{false};
    std::string data;
    std::string sender;
    std::chrono::system_clock::time_point timestamp;
    std::vector<std::shared_ptr<cluon::TCPConnection> > connections;

    REQUIRE(data.empty());
    REQUIRE(sender.empty());
    REQUIRE(!hasDataReceived);
    REQUIRE(connections.empty());

    cluon::TCPServer srv3(
        1235,
        [&hasDataReceived, &data, &sender, &timestamp, &connections](std::string &&from, std::shared_ptr<cluon::TCPConnection> connection) noexcept {
            sender = std::move(from);
            std::cout << "Got connection from " << sender << std::endl;
            connection->setOnNewData([&hasDataReceived, &data, &timestamp](std::string &&d, std::chrono::system_clock::time_point &&ts){
                data = std::move(d);
                timestamp = std::move(ts);
                hasDataReceived.store(true);
            });
            connection->setOnConnectionLost([](){
                std::cout << "Connection lost." << std::endl;
            });
            connections.push_back(connection);
        });

    REQUIRE(srv3.isRunning());

    cluon::TCPConnection conn3(
        "127.0.0.1", 1235,
        [](std::string &&, std::chrono::system_clock::time_point &&) noexcept {},
        [](){});

    std::string TEST_DATA{"Hello World"};
    const auto TEST_DATA_SIZE{TEST_DATA.size()};
    auto retVal2 = conn3.send(std::move(TEST_DATA));
    REQUIRE(TEST_DATA_SIZE == retVal2.first);
    REQUIRE(0 == retVal2.second);

    // Yield the UDP receiver so that the embedded thread has time to process the data.
    // Let the operating system spawn the thread.
    using namespace std::literals::chrono_literals; // NOLINT
    do { std::this_thread::sleep_for(1ms); } while (!hasDataReceived.load());

    REQUIRE(hasDataReceived);
    REQUIRE("Hello World" == data);
    REQUIRE(sender.substr(0, sender.find(':')) == "127.0.0.1");

    auto after = std::chrono::system_clock::now();

    // Test if the timestamping works correctly (only on 64bit systems).
    if (8 == sizeof(void *)) {
        REQUIRE(before < timestamp);
        REQUIRE(timestamp < after);
    }
}

//TEST_CASE("Testing multicast with 226.x.y.z address.") {
//    // Setup data structures to receive data from UDPReceiver.
//    std::atomic<bool> hasDataReceived{false};
//    std::string data;

//    REQUIRE(data.empty());
//    REQUIRE(!hasDataReceived);

//    cluon::UDPReceiver ur4(
//        "226.0.0.226", 1238, [&hasDataReceived, &data ](std::string && d, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
//            data = std::move(d);
//            hasDataReceived.store(true);
//        });
//    REQUIRE(ur4.isRunning());

//    // Setup UDPSender.
//    cluon::UDPSender us4{"226.0.0.226", 1238};
//    std::string TEST_DATA{"Hello Multicast Group"};
//    const auto TEST_DATA_SIZE{TEST_DATA.size()};
//    auto retVal4 = us4.send(std::move(TEST_DATA));
//    REQUIRE(TEST_DATA_SIZE == retVal4.first);
//    REQUIRE(0 == retVal4.second);

//    // Yield the UDP receiver so that the embedded thread has time to process the data.
//    do { std::this_thread::yield(); } while (!hasDataReceived.load());

//    REQUIRE(hasDataReceived);
//    REQUIRE("Hello Multicast Group" == data);
//}

//TEST_CASE("Testing multicast with faulty 224.0.0.1 address.") {
//    // Setup data structures to receive data from UDPReceiver.
//    std::atomic<bool> hasDataReceived{false};

//    REQUIRE(!hasDataReceived);

//    // Setup UDPReceiver.
//    cluon::UDPReceiver ur5{"224.0.0.1", 1239, nullptr};
//    REQUIRE(!ur5.isRunning());
//    REQUIRE(!hasDataReceived);
//}
