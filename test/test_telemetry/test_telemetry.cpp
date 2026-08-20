#include <math.h>
#include <unity.h>

#include "history.h"

using namespace telemetry;

static Sample make(uint32_t uptime, float temperature, float humidity, uint16_t light = 2000) {
    Sample sample;
    sample.uptime_s = uptime;
    sample.temperature_c = temperature;
    sample.humidity_pct = humidity;
    sample.light_raw = light;
    return sample;
}

void setUp(void) {}
void tearDown(void) {}

void test_a_new_history_is_empty(void) {
    History<8> history;
    TEST_ASSERT_EQUAL_UINT16(0, history.size());
    TEST_ASSERT_EQUAL_UINT16(8, history.capacity());
    TEST_ASSERT_FALSE(history.full());
}

void test_samples_come_back_oldest_first(void) {
    History<8> history;
    history.push(make(1, 20.0f, 50.0f));
    history.push(make(2, 21.0f, 51.0f));
    history.push(make(3, 22.0f, 52.0f));

    TEST_ASSERT_EQUAL_UINT16(3, history.size());
    TEST_ASSERT_EQUAL_UINT32(1, history.at(0).uptime_s);
    TEST_ASSERT_EQUAL_UINT32(3, history.at(2).uptime_s);
    TEST_ASSERT_EQUAL_UINT32(1, history.oldest().uptime_s);
    TEST_ASSERT_EQUAL_UINT32(3, history.newest().uptime_s);
}

void test_the_buffer_wraps_and_drops_the_oldest(void) {
    History<4> history;
    for (uint32_t i = 1; i <= 6; i++) {
        history.push(make(i, 20.0f + i, 50.0f));
    }

    TEST_ASSERT_TRUE(history.full());
    TEST_ASSERT_EQUAL_UINT16(4, history.size());
    TEST_ASSERT_EQUAL_UINT32(3, history.oldest().uptime_s);
    TEST_ASSERT_EQUAL_UINT32(6, history.newest().uptime_s);
}

void test_wrapping_many_times_keeps_the_order(void) {
    History<3> history;
    for (uint32_t i = 1; i <= 100; i++) {
        history.push(make(i, 20.0f, 50.0f));
    }

    TEST_ASSERT_EQUAL_UINT32(98, history.at(0).uptime_s);
    TEST_ASSERT_EQUAL_UINT32(99, history.at(1).uptime_s);
    TEST_ASSERT_EQUAL_UINT32(100, history.at(2).uptime_s);
}

void test_clearing_resets_the_buffer(void) {
    History<4> history;
    history.push(make(1, 20.0f, 50.0f));
    history.clear();

    TEST_ASSERT_EQUAL_UINT16(0, history.size());
    TEST_ASSERT_FALSE(history.full());
}

void test_stats_over_an_empty_history_are_zero(void) {
    History<4> history;
    Stats stats = history.temperature_stats();

    TEST_ASSERT_EQUAL_UINT16(0, stats.count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.mean);
}

void test_stats_report_min_max_and_mean(void) {
    History<8> history;
    history.push(make(1, 20.0f, 40.0f));
    history.push(make(2, 24.0f, 60.0f));
    history.push(make(3, 22.0f, 50.0f));

    Stats temperature = history.temperature_stats();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, temperature.min);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.0f, temperature.max);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, temperature.mean);
    TEST_ASSERT_EQUAL_UINT16(3, temperature.count);

    Stats humidity = history.humidity_stats();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, humidity.min);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, humidity.max);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, humidity.mean);
}

void test_stats_only_cover_what_is_still_in_the_buffer(void) {
    History<2> history;
    history.push(make(1, 0.0f, 10.0f));
    history.push(make(2, 30.0f, 20.0f));
    history.push(make(3, 30.0f, 30.0f));

    Stats stats = history.temperature_stats();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, stats.min);
    TEST_ASSERT_EQUAL_UINT16(2, stats.count);
}

void test_light_percent_maps_the_adc_range(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, light_percent(0));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, light_percent(4095));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, light_percent(2048));
}

void test_light_percent_clamps_above_full_scale(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, light_percent(9000));
}

void test_light_percent_survives_a_zero_full_scale(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, light_percent(100, 0));
}

void test_a_failed_dht_read_is_not_plausible(void) {
    TEST_ASSERT_FALSE(reading_is_plausible(NAN, 50.0f));
    TEST_ASSERT_FALSE(reading_is_plausible(22.0f, NAN));
}

void test_readings_outside_the_sensor_range_are_rejected(void) {
    TEST_ASSERT_FALSE(reading_is_plausible(-50.0f, 50.0f));
    TEST_ASSERT_FALSE(reading_is_plausible(120.0f, 50.0f));
    TEST_ASSERT_FALSE(reading_is_plausible(22.0f, 140.0f));
    TEST_ASSERT_FALSE(reading_is_plausible(22.0f, -5.0f));
}

void test_a_normal_indoor_reading_is_plausible(void) {
    TEST_ASSERT_TRUE(reading_is_plausible(23.4f, 48.0f));
    TEST_ASSERT_TRUE(reading_is_plausible(0.0f, 0.0f));
    TEST_ASSERT_TRUE(reading_is_plausible(-40.0f, 100.0f));
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_a_new_history_is_empty);
    RUN_TEST(test_samples_come_back_oldest_first);
    RUN_TEST(test_the_buffer_wraps_and_drops_the_oldest);
    RUN_TEST(test_wrapping_many_times_keeps_the_order);
    RUN_TEST(test_clearing_resets_the_buffer);

    RUN_TEST(test_stats_over_an_empty_history_are_zero);
    RUN_TEST(test_stats_report_min_max_and_mean);
    RUN_TEST(test_stats_only_cover_what_is_still_in_the_buffer);

    RUN_TEST(test_light_percent_maps_the_adc_range);
    RUN_TEST(test_light_percent_clamps_above_full_scale);
    RUN_TEST(test_light_percent_survives_a_zero_full_scale);

    RUN_TEST(test_a_failed_dht_read_is_not_plausible);
    RUN_TEST(test_readings_outside_the_sensor_range_are_rejected);
    RUN_TEST(test_a_normal_indoor_reading_is_plausible);

    return UNITY_END();
}
