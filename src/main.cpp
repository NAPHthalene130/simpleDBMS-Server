#include <chrono>
#include <iostream>
#include <thread>

#include "Core.h"

int main()
{
    Core core;
    core.start();

    std::cout << "simpleDBMS-Server started. Listening on port 10086." << std::endl;

    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }
}
