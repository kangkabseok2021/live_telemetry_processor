#include "RtEngine.h"
#include "c_api.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    imaging::RtEngine engine;
    engine.start();

    std::cout << "[demo] Sending CmdStart...\n";
    int rc = engine.sendCommand(SMACHINE_CMD_START);
    std::cout << "[demo] CmdStart result: " << rc
              << " (0=OK, 1=INVALID_TRANSITION)\n";

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "[demo] State index after 3s: " << engine.getStateId() << "\n";
    std::cout << "[demo] Deadline violations:  " << engine.deadlineViolations() << "\n";

    engine.stop();
    std::cout << "[demo] Done.\n";
    return 0;
}
