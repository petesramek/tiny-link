/**
 * @file SharedData.h
 * @brief Common payload type used by both the ATtiny88 sensor node and the
 *        ESP8266 gateway in all three IoT_Sensor_Gateway examples.
 *
 * Both devices instantiate:
 *   TinyLink<SensorMessage, TinyArduinoAdapter> link(adapter);
 *
 * The TinyLink wire type (TYPE_CMD / TYPE_DATA) tells the receiver which
 * direction the frame is travelling:
 *
 *   TYPE_CMD  ('C')  ESP8266 → ATtiny88  "please send a sensor reading"
 *   TYPE_DATA ('D')  ATtiny88 → ESP8266  sensor reading + uptime
 */

#pragma once
#include <stdint.h>
#include "protocol/MessageType.h"

struct SensorMessage {
    uint8_t  requestId;    /**< Request counter. Response echoes this value.    */
    float    temperature;  /**< Degrees Celsius. Set to 0.0 in request frames.  */
    uint32_t uptime;       /**< Sender's millis() at send time. 0 in requests.  */
} __attribute__((packed));

// Convenience constants — avoid scattering the cast everywhere.
static const uint8_t TYPE_CMD  =
    static_cast<uint8_t>(tinylink::MessageType::Cmd);
static const uint8_t TYPE_DATA =
    static_cast<uint8_t>(tinylink::MessageType::Data);
