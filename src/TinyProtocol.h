/**
 * @file TinyProtocol.h
 * @brief Umbrella header — re-exports all focused protocol type headers.
 *
 * New code may include the focused headers directly to reduce compile scope:
 *   #include "protocol/MessageType.h"
 *   #include "protocol/Status.h"
 *   #include "protocol/State.h"
 *   #include "protocol/Stats.h"
 *   #include "protocol/AckMessage.h"
 *   #include "protocol/DebugMessage.h"
 *
 * Existing code that includes TinyProtocol.h directly continues to work
 * without modification.
 */

#ifndef TINY_PROTOCOL_H
#define TINY_PROTOCOL_H

#include "protocol/MessageType.h"
#include "protocol/Status.h"
#include "protocol/State.h"
#include "protocol/Stats.h"
#include "protocol/AckMessage.h"
#include "protocol/DebugMessage.h"

#endif // TINY_PROTOCOL_H
