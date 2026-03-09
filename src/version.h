/**
 * @file version.h
 * @brief Version tracking for the TinyLink library.
 * 
 * Allows for compile-time feature detection and compatibility checks.
 */

#ifndef TINY_LINK_VERSION_H
#define TINY_LINK_VERSION_H

/**
 * @brief Semantic version integer: (Major * 10000) + (Minor * 100) + Patch.
 * Example: v0.4.0 = 400.
 */
#define TINYLINK_VERSION 400

#define TINYLINK_VERSION_MAJOR 0
#define TINYLINK_VERSION_MINOR 4
#define TINYLINK_VERSION_PATCH 0

/**
 * @brief Version as a string literal.
 */
#define TINYLINK_VERSION_STR "0.4.0"

#endif // TINY_LINK_VERSION_H
