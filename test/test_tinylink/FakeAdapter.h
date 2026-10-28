#ifndef TINYLINK_TEST_FAKEADAPTER_H
#define TINYLINK_TEST_FAKEADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "adapters/WriteResult.h"

namespace tinylink_test {

    /** @brief Deterministic in-memory adapter for TinyLink engine tests. */
    class FakeAdapter {
    public:
        /** @brief Maximum number of bytes retained in either test buffer. */
        static const size_t BUFFER_CAPACITY = 8192;

        FakeAdapter()
            : _open(true),
              _now(0),
              _inputLength(0),
              _inputPosition(0),
              _outputLength(0),
              _scriptLength(0),
              _scriptPosition(0)
        {}

        bool isOpen() const { return _open; }

        int available() const
        {
            return _open
                ? static_cast<int>(_inputLength - _inputPosition)
                : 0;
        }

        int read()
        {
            if (!_open || _inputPosition >= _inputLength) {
                return -1;
            }

            return _input[_inputPosition++];
        }

        tinylink::WriteResult write(uint8_t value)
        {
            return write(&value, 1);
        }

        tinylink::WriteResult write(const uint8_t* data, size_t length)
        {
            if (length == 0) {
                return tinylink::WriteResult::fromCount(0, 0);
            }

            if (!_open || !data ||
                length > BUFFER_CAPACITY - _outputLength) {
                return tinylink::WriteResult::failure();
            }

            size_t accepted = length;
            if (_scriptPosition < _scriptLength) {
                const size_t limit = _script[_scriptPosition++];
                if (limit < accepted) {
                    accepted = limit;
                }
            }

            for (size_t i = 0; i < accepted; ++i) {
                _output[_outputLength++] = data[i];
            }
            return tinylink::WriteResult::fromCount(length, accepted);
        }

        /**
         * @brief Script the next write calls: each entry is the number of
         * bytes that call accepts (0 fails it; less than requested is partial).
         */
        void scriptWrites(const size_t* limits, size_t count)
        {
            _scriptLength = count < SCRIPT_CAPACITY ? count : SCRIPT_CAPACITY;
            _scriptPosition = 0;
            for (size_t i = 0; i < _scriptLength; ++i) {
                _script[i] = limits[i];
            }
        }

        unsigned long millis() const { return _now; }

        void setOpen(bool open) { _open = open; }
        void setTime(unsigned long now) { _now = now; }
        void advanceTime(unsigned long milliseconds) { _now += milliseconds; }

        bool queueBytes(const uint8_t* data, size_t length)
        {
            if ((!data && length > 0) ||
                length > BUFFER_CAPACITY - _inputLength) {
                return false;
            }

            for (size_t i = 0; i < length; ++i) {
                _input[_inputLength++] = data[i];
            }
            return true;
        }

        bool queueByte(uint8_t value)
        {
            return queueBytes(&value, 1);
        }

        void clearInput()
        {
            _inputLength = 0;
            _inputPosition = 0;
        }

        const uint8_t* output() const { return _output; }
        size_t outputSize() const { return _outputLength; }

        void clearOutput() { _outputLength = 0; }

    private:
        bool _open;
        unsigned long _now;
        uint8_t _input[BUFFER_CAPACITY];
        size_t _inputLength;
        size_t _inputPosition;
        uint8_t _output[BUFFER_CAPACITY];
        size_t _outputLength;
        static const size_t SCRIPT_CAPACITY = 8;
        size_t _script[SCRIPT_CAPACITY];
        size_t _scriptLength;
        size_t _scriptPosition;
    };

} // namespace tinylink_test

#endif // TINYLINK_TEST_FAKEADAPTER_H
