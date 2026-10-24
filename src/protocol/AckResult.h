/**
 * @file protocol/Status.h
 * @brief ACK results carried by TinyLink acknowledgement frames.
 *
 * ACK results describe how a peer handled an identified application frame.
 * Local codec, framing, timeout, and buffer failures are telemetry events and
 * are not represented by this type.
 */
#ifndef TINYLINK_STATUS_H
#define TINYLINK_STATUS_H

#include <stdint.h>

namespace tinylink {

	/**
	 * @brief Result reported by a TinyLink acknowledgement frame.
	 *
	 * Values are stable one-byte wire identifiers. Undefined values are reserved
	 * for future protocol versions and must be rejected by current decoders.
	 */
	enum class AckResult : uint8_t {
		Ok = 0x00, /**< The acknowledged frame was handled successfully. */
		Busy = 0x04, /**< The frame was not accepted; the sender may retry later. */
		Unknown = 0xFF  /**< The peer could not provide a more specific result. */
	};

	static_assert(sizeof(AckResult) == 1,
		"AckResult must have an 8-bit representation");
	static_assert(static_cast<uint8_t>(AckResult::Ok) == 0x00,
		"AckResult::Ok wire value changed");
	static_assert(static_cast<uint8_t>(AckResult::Busy) == 0x04,
		"AckResult::Busy wire value changed");
	static_assert(static_cast<uint8_t>(AckResult::Unknown) == 0xFF,
		"AckResult::Unknown wire value changed");

	/**
	 * @brief Convert a wire byte to an AckResult.
	 *
	 * @param value Raw result byte from an acknowledgement payload.
	 * @param result Destination updated only when the value is valid.
	 * @return true when value is a declared AckResult; otherwise false.
	 */
	inline bool ack_result_from_wire(uint8_t value, AckResult& result) {
		switch (value) {
		case static_cast<uint8_t>(AckResult::Ok):
			result = AckResult::Ok;
			return true;

		case static_cast<uint8_t>(AckResult::Busy):
			result = AckResult::Busy;
			return true;

		case static_cast<uint8_t>(AckResult::Unknown):
			result = AckResult::Unknown;
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Convert a declared AckResult to its wire byte.
	 *
	 * @param result Declared acknowledgement result.
	 * @return Stable one-byte wire identifier.
	 */
	inline uint8_t ack_result_to_wire(AckResult result) {
		return static_cast<uint8_t>(result);
	}

	/**
	 * @brief Convert an AckResult to a human-readable diagnostic string.
	 *
	 * @param result Acknowledgement result.
	 * @return Static diagnostic string, or "Invalid" for an undeclared value.
	 */
	inline const char* ack_result_to_string(AckResult result) {
		switch (result) {
		case AckResult::Ok:
			return "Ok";

		case AckResult::Busy:
			return "Busy";

		case AckResult::Unknown:
			return "Unknown";

		default:
			return "Invalid";
		}
	}

} // namespace tinylink

#endif // TINYLINK_STATUS_H
