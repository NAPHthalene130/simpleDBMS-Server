#include "Core.h"

Core::Core() = default;

Core::~Core()
{
    stop();
}

void Core::start()
{
    netReceiver.start();
    netSender.start();
}

void Core::stop()
{
    netSender.stop();
    netReceiver.stop();
}
