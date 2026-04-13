#include "sim_server.h"

#include <iostream>
#include <string>

#include "sim_protocol.h"

int RunServer(SimController &controller)
{
    SimProtocol protocol(controller);
    std::string line;

    while (std::getline(std::cin, line))
    {
        if (line.empty())
        {
            continue;
        }

        std::cout << protocol.HandleLine(line) << '\n' << std::flush;
    }

    return 0;
}
