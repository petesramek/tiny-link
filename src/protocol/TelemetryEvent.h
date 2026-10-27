/**
 * @file protocol/TelemetryEvent.h
 * @brief Local telemetry events recorded by TinyLink.
 *
 * Telemetry events describe conditions observed by the local TinyLink
 * instance. They are not ACK results and are not transmitted as part of the
 * TinyLink wire protocol.
 */
#ifndef TINYLINK_TELEMETRYEVENT_H
#define TINYLINK_TELEMETRYEVENT_H

#include <stdint.h>

namespace tinylink {

	/**
	 * @brief Local events recorded for TinyLink diagnostics.
	 *
	 * These values classify local protocol, codec, transport, and state-machine
	 * events. Their numeric values are local implementation identifiers and must
	 * not be used as wire-protocol values.
	 */
	enum class TelemetryEvent : uint8_t {
		PacketReceived = 0,         /**< A complete application packet was received successfully. */
		CobsDecodeFailed = 1,       /**< A COBS-encoded frame could not be decoded. */
		ChecksumMismatch = 2,       /**< A decoded packet failed checksum validation. */
		PacketLengthMismatch = 3,   /**< The decoded packet length did not match its header. */
		PayloadSizeMismatch = 4,    /**< The packet payload did not fit the expected destination. */
		PacketEncodeFailed = 5,     /**< A packet could not be encoded into its plain representation. */
		CobsEncodeFailed = 6,       /**< A plain packet could not be COBS-encoded. */
		InterByteTimeout = 7,       /**< Reception of a partial frame exceeded the inter-byte timeout. */
		AckTimeout = 8,             /**< An expected acknowledgement was not received in time. */
		ReceiveOverflow = 9,        /**< An incoming encoded frame exceeded the receive buffer. */
		UnexpectedMessageType = 10, /**< A frame type was invalid or unexpected in the current context. */
		UnexpectedSequence = 11,    /**< A received sequence did not match the expected sequence. */
		InvalidAckResult = 12,      /**< An ACK payload contained an undefined result value. */
		InvalidHandshake = 13       /**< A handshake payload was malformed or unsupported. */
	};

	/**
	 * @brief Convert a telemetry event to a human-readable diagnostic string.
	 *
	 * @param event Telemetry event to describe.
	 * @return Static diagnostic string, or "Invalid" for an undeclared value.
	 */
	inline const char* telemetry_event_to_string(TelemetryEvent event) {
		switch (event) {
		case TelemetryEvent::PacketReceived:
			return "PacketReceived";

		case TelemetryEvent::CobsDecodeFailed:
			return "CobsDecodeFailed";

		case TelemetryEvent::ChecksumMismatch:
			return "ChecksumMismatch";

		case TelemetryEvent::PacketLengthMismatch:
			return "PacketLengthMismatch";

		case TelemetryEvent::PayloadSizeMismatch:
			return "PayloadSizeMismatch";

		case TelemetryEvent::PacketEncodeFailed:
			return "PacketEncodeFailed";

		case TelemetryEvent::CobsEncodeFailed:
			return "CobsEncodeFailed";

		case TelemetryEvent::InterByteTimeout:
			return "InterByteTimeout";

		case TelemetryEvent::AckTimeout:
			return "AckTimeout";

		case TelemetryEvent::ReceiveOverflow:
			return "ReceiveOverflow";

		case TelemetryEvent::UnexpectedMessageType:
			return "UnexpectedMessageType";

		case TelemetryEvent::UnexpectedSequence:
			return "UnexpectedSequence";

		case TelemetryEvent::InvalidAckResult:
			return "InvalidAckResult";

		case TelemetryEvent::InvalidHandshake:
			return "InvalidHandshake";

		default:
			return "Invalid";
		}
	}

} // namespace tinylink

#endif // TINYLINK_TELEMETRYEVENT_H
