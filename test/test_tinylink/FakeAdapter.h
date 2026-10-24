#ifndef TINYLINK_TEST_FAKEADAPTER_H
#define TINYLINK_TEST_FAKEADAPTER_H

#include <stddef.h>
#include <stdint.h>

namespace tinylink_test {

    class FakeAdapter {
    public:
        static const size_t BUFFER_CAPACITY = 8192;

        FakeAdapter()
            : _open(true),
              _now(0),
              _inputLength(0),
              _inputPosition(0),
              _outputLength(0)
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

        void write(uint8_t value)
        {
            if (_open && _outputLength < BUFFER_CAPACITY) {
                _output[_outputLength++] = value;
            }
        }

        void write(const uint8_t* data, size_t length)
        {
            if (!_open || !data ||
                length > BUFFER_CAPACITY - _outputLength) {
                return;
            }

            for (size_t i = 0; i < length; ++i) {
                _output[_outputLength++] = data[i];
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
    };

} // namespace tinylink_test

#endif // TINYLINK_TEST_FAKEADAPTER_H
