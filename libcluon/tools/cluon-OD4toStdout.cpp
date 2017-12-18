/*
 * Copyright (C) 2017  Christian Berger
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

#include "cluon/UDPReceiver.hpp"

#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv) {
    int retVal{1};
    const std::string PROGRAM{argv[0]}; // NOLINT
    if (2 != argc) {
        std::cerr << PROGRAM
                  << " dumps Containers received from an OpenDaVINCI v4 session to stdout." << std::endl;
        std::cerr << "Usage:   " << PROGRAM << " CID" << std::endl;
        std::cerr << "Example: " << PROGRAM << " 111" << std::endl;
    } else {
        const std::string ADDRESS{"225.0.0." + std::string(argv[1])}; // NOLINT
        constexpr uint16_t PORT{12175};

        cluon::UDPReceiver receiver(
            ADDRESS, PORT,
            [](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
                std::cout << data;
                std::cout.flush();
            });

        using namespace std::literals::chrono_literals; // NOLINT
        while (receiver.isRunning()) {
            std::this_thread::sleep_for(1s);
        }
        retVal = 0;
    }
    return retVal;
}
