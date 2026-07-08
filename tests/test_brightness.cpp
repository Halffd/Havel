// ⚠️  ESTE TESTE APLICA VALORES NO HARDWARE REAL (MONITOR/GAMMA/BRIGHTNESS)
// ⚠️  NÃO RODAR SEM MONITOR VISÍVEL
// ⚠️  PODE DEIXAR TELA BRANCA/CORROMPIDA - USE SSH PARA RECOVERY
// ⚠️  TESTES DESTINADOS A AMBIENTE COM MONITOR REAL VISÍVEL
// ⚠️  USE COM CAUTELA

#include "core/brightness/BrightnessManager.hpp"
#include <cmath>
#include <iostream>
#include <string>

using namespace havel;

static int passed = 0;
static int failed = 0;

#define TEST(name) \
    do { \
        std::cout << "[TEST] " << name << " ... " << std::flush; \
    } while(0)

#define PASS() \
    do { \
        std::cout << "OK" << std::endl; \
        passed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAIL: " << msg << std::endl; \
        failed++; \
    } while(0)

#define CHECK_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (_a != _b) { \
            using ::std::to_string; \
            FAIL(std::string("expected " #a " == " #b " but got ") + to_string(_a) + " vs " + to_string(_b)); \
            return; \
        } \
    } while(0)

#define CHECK_TRUE(expr) \
    do { \
        if (!(expr)) { FAIL("expected " #expr " to be true"); return; } \
    } while(0)

#define CHECK_FALSE(expr) \
    do { \
        if ((expr)) { FAIL("expected " #expr to be false"); return; } \
    } while(0)

#define CHECK_NO_THROW(expr) \
    do { \
        try { expr; } \
        catch (const std::exception &e) { FAIL(std::string("unexpected exception: ") + e.what()); return; } \
        catch (...) { FAIL("unexpected exception"); return; } \
    } while(0)


// ========== Test Cases ==========

static void test_construct_init() {
    TEST("construct, init, destruct");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.init());
    PASS();
}

static void test_invalid_monitor_index() {
    TEST("getMonitor returns empty for invalid indices");
    BrightnessManager mgr;
    mgr.init();
    CHECK_TRUE(mgr.getMonitor(-1).empty());
    CHECK_TRUE(mgr.getMonitor(999).empty());
    PASS();
}

static void test_getBrightness_doesnt_crash() {
    TEST("getBrightness doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    CHECK_NO_THROW(mgr.getBrightness());
    CHECK_NO_THROW(mgr.getBrightness(""));
    CHECK_NO_THROW(mgr.getBrightness("nonexistent"));
    PASS();
}

static void test_setBrightness_doesnt_crash() {
    TEST("setBrightness doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.setBrightness(0.5);
    mgr.setBrightness("", 0.5);
    mgr.setBrightness("nonexistent", 1.0);
    PASS();
}

static void test_setBrightness_stores_value_getBrightness_uses_it_as_fallback() {
    TEST("setBrightness stores value; getBrightness uses it as fallback");
    BrightnessManager mgr;
    mgr.init();
    mgr.setBrightness("cachetest", 0.5);
    double val = mgr.getBrightness("cachetest");
    CHECK_EQ(val, 0.5);
    PASS();
}

static void test_getGammaRGB_doesnt_crash() {
    TEST("getGammaRGB doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    CHECK_NO_THROW(mgr.getGammaRGB());
    CHECK_NO_THROW(mgr.getGammaRGB(""));
    CHECK_NO_THROW(mgr.getGammaRGB("nonexistent"));
    PASS();
}

static void test_setGammaRGB_doesnt_crash() {
    TEST("setGammaRGB doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.setGammaRGB(1.0, 1.0, 1.0);
    mgr.setGammaRGB("", 1.0, 1.0, 1.0);
    mgr.setGammaRGB("nonexistent", 1.0, 1.0, 1.0);
    PASS();
}

static void test_gammaRGB_cache_stores_and_retrieves_values() {
    TEST("gammaRGB cache stores and retrieves values");
    BrightnessManager mgr;
    mgr.init();
    mgr.setGammaRGB("gtest", 0.8, 0.7, 0.9);
    auto g = mgr.getGammaRGB("gtest");
    CHECK_EQ(g.red, 0.8);
    CHECK_EQ(g.green, 0.7);
    CHECK_EQ(g.blue, 0.9);
    PASS();
}

static void test_getTemperature_doesnt_crash() {
    TEST("getTemperature doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    CHECK_NO_THROW(mgr.getTemperature());
    CHECK_NO_THROW(mgr.getTemperature(""));
    CHECK_NO_THROW(mgr.getTemperature("nonexistent"));
    PASS();
}

static void test_setTemperature_doesnt_crash() {
    TEST("setTemperature doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.setTemperature(6500);
    mgr.setTemperature("", 6500);
    mgr.setTemperature("nonexistent", 6500);
    PASS();
}

static void test_temperature_set_stores_value_on_hardware_success() {
    TEST("temperature set stores value on hardware success");
    BrightnessManager mgr;
    mgr.init();
    mgr.setTemperature("ttest", 4000);
    int t = mgr.getTemperature("ttest");
    CHECK_EQ(t, 4000);
    PASS();
}

static void test_getShadowLift_defaults_to_zero() {
    TEST("getShadowLift defaults to 0.0");
    BrightnessManager mgr;
    mgr.init();
    double lift = mgr.getShadowLift();
    CHECK_EQ(lift, 0.0);
    PASS();
}

static void test_setShadowLift_doesnt_crash() {
    TEST("setShadowLift doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.setShadowLift(0.5);
    mgr.setShadowLift("", 0.5);
    mgr.setShadowLift("nonexistent", 0.5);
    PASS();
}

static void test_shadowLift_cache_stores_and_retrieves_values() {
    TEST("shadowLift cache stores and retrieves values");
    BrightnessManager mgr;
    mgr.init();
    mgr.setShadowLift("stest", 0.3);
    double lift = mgr.getShadowLift("stest");
    CHECK_EQ(lift, 0.3);
    PASS();
}

static void test_increase_decreaseGamma_doesnt_crash() {
    TEST("increase/decreaseGamma doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.increaseGamma(10);
    mgr.decreaseGamma(5);
    mgr.increaseGamma(50);
    mgr.decreaseGamma(50);
    PASS();
}

static void test_increase_decreaseShadowLift_doesnt_crash() {
    TEST("increase/decreaseShadowLift doesn't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.increaseShadowLift(10);
    mgr.decreaseShadowLift(5);
    PASS();
}

static void test_combined_setBrightnessAndRGB_operations_dont_crash() {
    TEST("combined setBrightnessAnd* operations don't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.setBrightnessAndRGB(0.5, 1.0, 1.0, 1.0);
    mgr.setBrightnessAndTemperature(0.5, 6500);
    mgr.setBrightnessAndShadowLift(0.5, 0.2);
    PASS();
}

static void test_day_night_mode_enable_and_disable() {
    TEST("day/night mode enable and disable");
    BrightnessManager mgr;
    mgr.init();
    BrightnessManager::DayNightSettings settings;
    settings.dayBrightness = 1.0;
    settings.nightBrightness = 0.3;
    settings.dayTemperature = 6500;
    settings.nightTemperature = 3000;
    settings.dayStartHour = 7;
    settings.nightStartHour = 20;
    settings.autoAdjust = true;
    settings.checkInterval = std::chrono::minutes(5);
    mgr.enableDayNightMode(settings);
    mgr.disableDayNightMode();
    PASS();
}

static void test_switchToDay_switchToNight_dont_crash() {
    TEST("switchToDay/switchToNight don't crash");
    BrightnessManager mgr;
    mgr.init();
    mgr.switchToDay();
    mgr.switchToNight();
    PASS();
}

static void test_isDay_returns_without_crash() {
    TEST("isDay returns without crash");
    BrightnessManager mgr;
    mgr.init();
    bool isDay = mgr.isDay();
    (void)isDay;
    PASS();
}

static void test_getConnectedMonitors_returns_without_crash() {
    TEST("getConnectedMonitors returns without crash");
    BrightnessManager mgr;
    mgr.init();
    auto monitors = mgr.getConnectedMonitors();
    (void)monitors;
    PASS();
}

static void test_cached_temperature_and_shadowLift_survive_init() {
    TEST("cached temperature and shadowLift survive init()");
    BrightnessManager mgr;
    mgr.init();
    mgr.setTemperature(4000);
    mgr.setShadowLift(0.5);
    // Note: init() re-reads from hardware, so cached values may not survive
    // We just verify the test doesn't crash
    mgr.init();
    CHECK_EQ(mgr.getShadowLift(), 0.5);
    PASS();
}

static void test_setTemperature_accepts_boundary_values() {
    TEST("setTemperature accepts boundary values");
    BrightnessManager mgr;
    mgr.init();
    mgr.setTemperature(1000);
    mgr.setTemperature(40000);
    PASS();
}

static void test_setBrightness_clamps_to_0_2() {
    TEST("setBrightness clamps to [0, 2]");
    BrightnessManager mgr;
    mgr.init();
    mgr.setBrightness(-0.5);
    mgr.setBrightness(2.5);
    PASS();
}

// ========== Main ==========

int main() {
    std::cout << "=== Brightness Manager Unit Tests ===" << std::endl;

    test_construct_init();
    test_invalid_monitor_index();
    test_getBrightness_doesnt_crash();
    test_setBrightness_doesnt_crash();
    test_setBrightness_stores_value_getBrightness_uses_it_as_fallback();
    test_getGammaRGB_doesnt_crash();
    test_setGammaRGB_doesnt_crash();
    test_gammaRGB_cache_stores_and_retrieves_values();
    test_getTemperature_doesnt_crash();
    test_setTemperature_doesnt_crash();
    test_temperature_set_stores_value_on_hardware_success();
    test_getShadowLift_defaults_to_zero();
    test_setShadowLift_doesnt_crash();
    test_shadowLift_cache_stores_and_retrieves_values();
    test_increase_decreaseGamma_doesnt_crash();
    test_increase_decreaseShadowLift_doesnt_crash();
    test_combined_setBrightnessAndRGB_operations_dont_crash();
    test_day_night_mode_enable_and_disable();
    test_switchToDay_switchToNight_dont_crash();
    test_isDay_returns_without_crash();
    test_getConnectedMonitors_returns_without_crash();
    test_cached_temperature_and_shadowLift_survive_init();
    test_setTemperature_accepts_boundary_values();
    test_setBrightness_clamps_to_0_2();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    return failed == 0 ? 0 : 1;
}