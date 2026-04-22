#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "Core.h"
#include "log/LogWriter.h"

int main()
{
    try {
        LogWriter::info("app", "main", "main", "Server process is starting.");

        Core core;
        core.start();

        LogWriter::info("app", "main", "main", "Server started and entered keep-alive loop.");
        std::cout << "simpleDBMS-Server started. Listening on port 10086." << std::endl;

        while (true) {
            std::this_thread::sleep_for(std::chrono::hours(24));
        }
    } catch (const std::exception &exception) {
        LogWriter::fatal("app", "main", "main", std::string("Unhandled exception: ") + exception.what());
    } catch (...) {
        LogWriter::fatal("app", "main", "main", "Unhandled unknown exception.");
    }

    return 1;
}
