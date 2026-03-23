/**
 * @file TinyProtocol.h
 * @brief Umbrella header — re-exports public protocol type headers.
 *
 * Payload wire-format headers (TinyAck, LogMessage) are internal
 * implementation details and are not re-exported here. Include them
 * explicitly from their internal paths if needed:
 *   #include "protocol/internal/AckMessage.h"
 *   #include "protocol/internal/LogMessage.h"
 */

#ifndef TINY_PROTOCOL_H
#define TINY_PROTOCOL_H

#include "protocol/MessageType.h"
#include "protocol/Status.h"
#include "protocol/State.h"
#include "protocol/Stats.h"

#endif // TINY_PROTOCOL_H
