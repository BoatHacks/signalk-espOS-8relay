#include "switch_bank_pgn.h"
#include "unity.h"

TEST_CASE("127501: all off, channels 9-28 unavailable", "[pgn]")
{
    uint8_t p[8];
    switch_bank_encode_status(7, 0x00, 8, p);
    const uint8_t want[8] = {7, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, p, 8);
}

TEST_CASE("127501: channel n is bits 2(n-1)..2(n-1)+1 after the instance", "[pgn]")
{
    uint8_t p[8];
    switch_bank_encode_status(0, 0x81, 8, p);  // channels 1 and 8 on
    TEST_ASSERT_EQUAL_HEX8(0x01, p[1]);        // ch1..4: on, off, off, off
    TEST_ASSERT_EQUAL_HEX8(0x40, p[2]);        // ch5..8: off, off, off, on
    switch_bank_encode_status(0, 0xFF, 8, p);
    TEST_ASSERT_EQUAL_HEX8(0x55, p[1]);
    TEST_ASSERT_EQUAL_HEX8(0x55, p[2]);
}

TEST_CASE("127502: on, off and no-change fields", "[pgn]")
{
    // ch1 on, ch2 off, ch3 no change, ch4 reserved; ch5 no change, ch6 on,
    // ch7 no change, ch8 off; ch9+ on (beyond our 8 channels)
    const uint8_t p[8] = {3, 0xB1, 0x37, 0x55, 0x55, 0x55, 0x55, 0x55};
    switch_bank_command_t c;
    TEST_ASSERT_TRUE(switch_bank_decode_control(p, 8, 3, 8, &c));
    TEST_ASSERT_EQUAL_HEX32(0x21, c.on);   // channels 1, 6
    TEST_ASSERT_EQUAL_HEX32(0x82, c.off);  // channels 2, 8
}

TEST_CASE("127502 for another instance or too short is ignored", "[pgn]")
{
    const uint8_t p[8] = {4, 0x55, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    switch_bank_command_t c = {0xDEAD, 0xBEEF};
    TEST_ASSERT_FALSE(switch_bank_decode_control(p, 8, 3, 8, &c));
    TEST_ASSERT_FALSE(switch_bank_decode_control(p, 7, 4, 8, &c));
    TEST_ASSERT_EQUAL_HEX32(0xDEAD, c.on);
}

TEST_CASE("status and control round-trip", "[pgn]")
{
    for (uint32_t mask = 0; mask < 256; mask++) {
        uint8_t p[8];
        switch_bank_encode_status(9, mask, 8, p);
        switch_bank_command_t c;
        TEST_ASSERT_TRUE(switch_bank_decode_control(p, 8, 9, 8, &c));
        TEST_ASSERT_EQUAL_HEX32(mask, c.on);
        TEST_ASSERT_EQUAL_HEX32(~mask & 0xFF, c.off);
    }
}
