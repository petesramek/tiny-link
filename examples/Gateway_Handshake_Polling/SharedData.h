#include <stdint.h>
namespace tinylink {
    struct SensorData {
        uint32_t sensorId;
        float value;
    };
    
    struct GatewayStatus {
        uint8_t ready; // 1 = Ready, 0 = Busy/Error
        uint32_t timestamp;
    };
}
