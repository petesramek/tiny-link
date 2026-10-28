#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

void registerLifecycleTests();
void registerApplicationTests();
void registerAckSequenceTests();
void registerReceiveMailboxTests();
void registerTimeoutTests();
void registerOverflowRecoveryTests();
void registerLoggingTests();
void registerTelemetryTests();

int main()
{
    UNITY_BEGIN();
    registerLifecycleTests();
    registerApplicationTests();
    registerAckSequenceTests();
    registerReceiveMailboxTests();
    registerTimeoutTests();
    registerOverflowRecoveryTests();
    registerLoggingTests();
    registerTelemetryTests();
    return UNITY_END();
}

extern "C" void setUp(void)
{
    tinylink_test::resetCallbacks();
}

extern "C" void tearDown(void)
{
}
