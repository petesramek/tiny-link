#include <iostream>
#include <iomanip>
#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"

using namespace tinylink;

// 1. Choose the correct adapter for your OS
#ifdef _WIN32
    #include "../../src/adapters/TinyWindowsAdapter.h"
    typedef TinyWindowsAdapter PCAdapter;
    const char* PORT = "COM3"; // Change to your COM port
#else
    #include "../../src/adapters/TinyPosixAdapter.h"
    typedef TinyPosixAdapter PCAdapter;
    const char* PORT = "/dev/ttyUSB0"; // Change to your TTY device
#endif

// 2. Callback for when the MCU sends data to the PC
void onMcuData(const MyData& data) {
    std::cout << "\n[INCOMING] "
              << "Uptime: " << data.uptime << "ms | "
              << "Temp: " << std::fixed << std::setprecision(2) << data.temperature << "°C | "
              << "Cmd: " << (int)data.commandId << std::endl;
}

int main() {
    // 3. Initialize Hardware Adapter
    PCAdapter hw(PORT, 9600);
    if (!hw.isOpen()) {
        std::cerr << "Failed to open port " << PORT << std::endl;
        return 1;
    }

    // 4. Initialize TinyLink
    TinyLink<MyData, PCAdapter> link(hw);
    link.onReceive(onMcuData);

    std::cout << "TinyLink Monitor started on " << PORT << "..." << std::endl;
    std::cout << "Press Ctrl+C to exit." << std::endl;

    while (true) {
        link.update(); // Continuous polling for COBS frames

        // Example: Send a command from PC to MCU every 5 seconds
        static auto lastSend = hw.millis();
        if (hw.millis() - lastSend > 5000) {
            MyData cmd = { (uint32_t)hw.millis(), 0.0f, 1 };
            link.sendData(TYPE_DATA, cmd);
            std::cout << "[SENT] Command 1 to MCU" << std::endl;
            lastSend = hw.millis();
        }
    }

    return 0;
}
