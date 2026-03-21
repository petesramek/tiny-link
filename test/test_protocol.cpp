#include <unity.h>



// Existing tests... 

// End of existing tests

// Appending the missing main() function

/**
 * @brief Entry point for the PlatformIO Native Test Runner.
 * 
 * Final v1.0.0 "Space-Grade" Verification Suite: 36 Tests.
 * verified against AVR (8-bit) and ESP/PC (32/64-bit) architectures.
 */
int main(int argc, char **argv) {
    UNITY_BEGIN();

    // --- Core Protocol & Asynchronous Events ---
    RUN_TEST(test_cobs_loopback);                /**< Basic encode/decode/verify pipeline */
    RUN_TEST(test_cobs_zero_payload);            /**< COBS transparency with all-zero data */
    RUN_TEST(test_async_callback);               /**< Asynchronous event trigger verification */
    RUN_TEST(test_callback_burst);               /**< Handling multiple packets in one update cycle */

    // --- Synchronization & Noise Resilience ---
    RUN_TEST(test_cobs_resync);                  /**< Recovery after leading junk/serial noise */
    RUN_TEST(test_cobs_double_delimiter);        /**< Handling of consecutive 0x00 delimiters */
    RUN_TEST(test_double_delimiter_resilience);  /**< Stuttering sync-byte handling (noise filter) */
    RUN_TEST(test_mid_frame_sync_reset);         /**< Forced reset via 0x00 during partial frame */
    RUN_TEST(test_cold_boot_sync);               /**< Graceful handling of power-on delimiter surges */
    RUN_TEST(test_micro_frame_noise_suppression);/**< High-frequency noise spike filtering */

    // --- Integrity, Safety & Memory Protection ---
    RUN_TEST(test_cobs_crc_failure);             /**< Fletcher-16 bit-corruption detection */
    RUN_TEST(test_timeout_cleanup);              /**< Automated partial frame buffer recovery */
    RUN_TEST(test_buffer_overflow_protection);   /**< Runaway TX/noise memory safety reset */
    RUN_TEST(test_buffer_isolation);             /**< Valid peek() data protection during RX */
    RUN_TEST(test_cobs_read_overflow_safety);    /**< Decoder boundary-check protection */
    RUN_TEST(test_ghost_zero_detection);         /**< Detection of bit-flips resulting in 0x00 */
    RUN_TEST(test_single_bit_flip_detection);    /**< High-sensitivity single bit-level integrity */
    RUN_TEST(test_swapped_byte_detection);       /**< Positional sensitivity (swapped byte detection) */

    // --- Logic, Boundaries & State Machine ---
    RUN_TEST(test_type_filtering);               /**< Message type separation and identification */
    RUN_TEST(test_sequence_tracking);            /**< Packet ID incrementation verification */
    RUN_TEST(test_sequence_wrap_around);         /**< Counter rollover (255 -> 0) stability */
    RUN_TEST(test_float_extremes);               /**< Binary transparency for NAN/INF values */
    RUN_TEST(test_type_validation);              /**< Multi-struct instance identification */
    RUN_TEST(test_structural_size_mismatch);     /**< Rejection of valid COBS frames with wrong size */

    // --- Advanced Timing & Serial Pressure ---
    RUN_TEST(test_timeout_boundary);             /**< Precision check of the 250ms window */
    RUN_TEST(test_timeout_precision);            /**< Strict inter-byte timeout enforcement */
    RUN_TEST(test_zero_gap_stress);              /**< Performance under back-to-back TX stress */
    RUN_TEST(test_minimal_interframe_spacing);   /**< High-speed burst synchronization */
    RUN_TEST(test_payload_zero_transparency);    /**< Transparency check for zeros at data boundaries */

    // --- Structural Limits ---
    RUN_TEST(test_short_frame_rejection);        /**< Rejection of frames below protocol minimum */
    RUN_TEST(test_zero_length_header_rejection); /**< Malformed length-field protection */
    RUN_TEST(test_minimum_payload);              /**< Robustness with 1-byte struct payloads */
    RUN_TEST(test_leading_zero_payload);         /**< Encoding check for zeros at byte-0 */
    RUN_TEST(test_trailing_zero_payload);        /**< Encoding check for zeros at final byte */
    RUN_TEST(test_protocol_mtu_limit);           /**< Compile-time safety for MTU boundaries */
    RUN_TEST(test_split_integer_alignment);      /**< Multi-byte reassembly safety */
    RUN_TEST(test_reentrant_lock_safety);        /**< Buffer locking during user processing */
    RUN_TEST(test_endless_delimiter_resilience); /**< Protection against stuck-low bus */
    RUN_TEST(test_fletcher_contrast_integrity);  /**< Checksum accumulator stress test */
    RUN_TEST(test_endian_checksum_consistency);  /**< Cross-platform checksum logic */
    RUN_TEST(test_fragmented_arrival_persistence);/**< Resistance to serial jitter */
    RUN_TEST(test_signed_boundary_integrity);    /**< Signed/Unsigned bit transparency */
    RUN_TEST(test_callback_hotswap_safety);      /**< Dynamic memory/pointer safety */
    RUN_TEST(test_split_integer_alignment);      /**< Re-verifying integer boundaries */

    RUN_TEST(test_checksum_independence);     /**< Calculation isolation */
    RUN_TEST(test_cobs_truncation_safety);     /**< Truncated frame safety */
    RUN_TEST(test_mid_payload_delimiter_reset);/**< Structural reset resilience */
    RUN_TEST(test_zero_sum_integrity);         /**< Zero-checksum bit-transparency */
    RUN_TEST(test_trailing_bit_integrity);     /**< Full-buffer coverage check */

    RUN_TEST(test_fletcher_modulo_boundary);       /**< Modulo-255 math check */
    RUN_TEST(test_tight_frame_overlap);            /**< Back-to-back sync stress */
    RUN_TEST(test_callback_deregistration_safety); /**< Null-pointer hot-swap safety */

    RUN_TEST(test_struct_packing_integrity);
    RUN_TEST(test_unflushed_data_protection);
    RUN_TEST(test_primitive_type_support);
    RUN_TEST(test_truncated_cobs_rejection);
    RUN_TEST(test_fletcher_overflow_stability);
    RUN_TEST(test_crc_endian_sensitivity);

    return UNITY_END();
}