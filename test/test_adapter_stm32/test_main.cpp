#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#include "stm32_hal_stub.h"

#define HAL_UART_MODULE_ENABLED
#define STM32
#include <adapters/TinySTM32HALAdapter.h>
#include <TinyLink.h>

namespace {

    static_assert(
        tinylink::is_valid_adapter<tinylink::TinySTM32HALAdapter>::value,
        "STM32 adapter must satisfy TinyLink's transport contract");

    int peripheral;
    UART_HandleTypeDef handle;
    tinylink::TinySTM32HALAdapter adapter(&handle);

    void testStm32AdapterChecksHandleInstanceAndReceiveFlag()
    {
        TEST_ASSERT_TRUE(adapter.isOpen());
        TEST_ASSERT_EQUAL_INT(0, adapter.available());
        tinylinkHalReceiveReady = true;
        TEST_ASSERT_EQUAL_INT(1, adapter.available());

        handle.Instance = nullptr;
        TEST_ASSERT_FALSE(adapter.isOpen());
        TEST_ASSERT_EQUAL_INT(0, adapter.available());
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        adapter.write(1);
        adapter.write(nullptr, 0);
        TEST_ASSERT_EQUAL_UINT32(0, tinylinkHalTransmitCalls);
        handle.Instance = &peripheral;
    }

    void testStm32AdapterReadsOneByteWithBoundedTimeout()
    {
        tinylinkHalReceiveValue = 0xFEu;
        tinylinkHalReceiveStatus = HAL_OK;
        TEST_ASSERT_EQUAL_INT(0xFE, adapter.read());
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkHalReceiveCalls);
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkHalReceiveTimeout);

        tinylinkHalReceiveStatus = HAL_ERROR;
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        TEST_ASSERT_EQUAL_UINT32(2, tinylinkHalReceiveCalls);
    }

    void testStm32AdapterWritesSingleAndBufferWithDocumentedTimeouts()
    {
        const uint8_t bytes[] = { 0x12u, 0x00u, 0xFEu };
        adapter.write(0xA5u);
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkHalTransmitCalls);
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkHalTransmitLength);
        TEST_ASSERT_EQUAL_UINT32(10, tinylinkHalTransmitTimeout);
        adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL_UINT32(2, tinylinkHalTransmitCalls);
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), tinylinkHalTransmitLength);
        TEST_ASSERT_EQUAL_UINT32(100, tinylinkHalTransmitTimeout);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            bytes, tinylinkHalTransmittedBytes, sizeof(bytes));

        tinylinkHalTransmitStatus = HAL_ERROR;
        adapter.write(0xA6u);
        TEST_ASSERT_EQUAL_UINT32(3, tinylinkHalTransmitCalls);
        TEST_ASSERT_EQUAL_UINT32(10, tinylinkHalTransmitTimeout);
    }

    void testStm32AdapterRejectsInvalidAndOversizedWrites()
    {
        const uint8_t byte = 0x55u;
        adapter.write(nullptr, 0);
        adapter.write(nullptr, 1);
        adapter.write(&byte, static_cast<size_t>(UINT16_MAX) + 1u);
        TEST_ASSERT_EQUAL_UINT32(0, tinylinkHalTransmitCalls);

        static const uint8_t maximumTransfer[UINT16_MAX] = {};
        adapter.write(maximumTransfer, sizeof(maximumTransfer));
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkHalTransmitCalls);
        TEST_ASSERT_EQUAL_UINT32(UINT16_MAX, tinylinkHalTransmitLength);
    }

    void testStm32AdapterReadsHalTickWithoutTakingHandleOwnership()
    {
        tinylinkHalTick = 0x12345678u;
        TEST_ASSERT_EQUAL_UINT32(0x12345678u, adapter.millis());
        TEST_ASSERT_EQUAL_PTR(&peripheral, handle.Instance);
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testStm32AdapterChecksHandleInstanceAndReceiveFlag);
    RUN_TEST(testStm32AdapterReadsOneByteWithBoundedTimeout);
    RUN_TEST(testStm32AdapterWritesSingleAndBufferWithDocumentedTimeouts);
    RUN_TEST(testStm32AdapterRejectsInvalidAndOversizedWrites);
    RUN_TEST(testStm32AdapterReadsHalTickWithoutTakingHandleOwnership);
    return UNITY_END();
}

extern "C" void setUp(void)
{
    peripheral = 0;
    handle.Instance = &peripheral;
    tinylinkHalReceiveReady = false;
    tinylinkHalReceiveStatus = HAL_OK;
    tinylinkHalReceiveValue = 0;
    tinylinkHalReceiveTimeout = 0;
    tinylinkHalReceiveCalls = 0;
    tinylinkHalTransmitStatus = HAL_OK;
    tinylinkHalTransmitTimeout = 0;
    tinylinkHalTransmitLength = 0;
    tinylinkHalTransmitCalls = 0;
    tinylinkHalTick = 0;
}

extern "C" void tearDown(void)
{
}
