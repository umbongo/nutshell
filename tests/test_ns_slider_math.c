#include "test_framework.h"
#include "ns_slider_math.h"

int test_ns_slider_value_at(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ns_slider_value_at(0, 0, 420, -12, 30), -12);
    ASSERT_EQ(ns_slider_value_at(420, 0, 420, -12, 30), 30);
    ASSERT_EQ(ns_slider_value_at(120, 0, 420, -12, 30), 0);
    ASSERT_EQ(ns_slider_value_at(-5, 0, 420, -12, 30), -12);
    ASSERT_EQ(ns_slider_value_at(999, 0, 420, -12, 30), 30);
    ASSERT_EQ(ns_slider_value_at(50, 0, 0, -12, 30), -12);
    ASSERT_EQ(ns_slider_value_at(50, 0, -3, -12, 30), -12);
    /* offset track and rounding: 5 px of a 100 px 0..10 track is 0.5 -> 1 */
    ASSERT_EQ(ns_slider_value_at(35, 30, 100, 0, 10), 1);
    ASSERT_EQ(ns_slider_value_at(34, 30, 100, 0, 10), 0);
    TEST_END();
}

int test_ns_slider_x_of(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ns_slider_x_of(-12, 0, 420, -12, 30), 0);
    ASSERT_EQ(ns_slider_x_of(30, 0, 420, -12, 30), 420);
    ASSERT_EQ(ns_slider_x_of(0, 0, 420, -12, 30), 120);
    ASSERT_EQ(ns_slider_x_of(99, 10, 420, -12, 30), 430);
    ASSERT_EQ(ns_slider_x_of(-99, 10, 420, -12, 30), 10);
    ASSERT_EQ(ns_slider_x_of(5, 7, 0, -12, 30), 7);
    for (int v = -12; v <= 30; v++)
        ASSERT_EQ(ns_slider_value_at(ns_slider_x_of(v, 8, 300, -12, 30), 8, 300,
                                     -12, 30), v);
    TEST_END();
}

int test_ns_slider_key(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ns_slider_key(0, 0x25, -12, 30), -1);
    ASSERT_EQ(ns_slider_key(0, 0x28, -12, 30), -1);
    ASSERT_EQ(ns_slider_key(0, 0x27, -12, 30), 1);
    ASSERT_EQ(ns_slider_key(0, 0x26, -12, 30), 1);
    ASSERT_EQ(ns_slider_key(0, 0x22, -12, 30), -6);
    ASSERT_EQ(ns_slider_key(0, 0x21, -12, 30), 6);
    ASSERT_EQ(ns_slider_key(7, 0x24, -12, 30), -12);
    ASSERT_EQ(ns_slider_key(7, 0x23, -12, 30), 30);
    ASSERT_EQ(ns_slider_key(7, 0x41, -12, 30), 7);
    ASSERT_EQ(ns_slider_key(-12, 0x25, -12, 30), -12);
    ASSERT_EQ(ns_slider_key(-10, 0x22, -12, 30), -12);
    ASSERT_EQ(ns_slider_key(30, 0x27, -12, 30), 30);
    ASSERT_EQ(ns_slider_key(28, 0x21, -12, 30), 30);
    TEST_END();
}
