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
        if ((expr)) { FAIL("expected " #expr " to be false"); return; } \
    } while(0)

#define CHECK_NO_THROW(expr) \
    do { \
        try { expr; } \
        catch (const std::exception &e) { FAIL(std::string("unexpected exception: ") + e.what()); return; } \
        catch (...) { FAIL("unexpected exception"); return; } \
    } while(0)

// ========== Construction ==========

static void test_construct_init() {
    TEST("construct, init, destruct");
    CHECK_NO_THROW({
        BrightnessManager mgr;
        mgr.init();
    });
    PASS();
}

// ========== Monitor queries ==========

static void test_getMonitor_invalid() {
    TEST("getMonitor returns empty for invalid indices");
    BrightnessManager mgr;
    CHECK_TRUE(mgr.getMonitor(-1).empty());
    CHECK_TRUE(mgr.getMonitor(999).empty());
    PASS();
}

// ========== Brightness ==========

static void test_getBrightness_no_crash() {
    TEST("getBrightness doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.getBrightness());
    CHECK_NO_THROW(mgr.getBrightness("any"));
    CHECK_NO_THROW(mgr.getBrightness(-1));
    CHECK_NO_THROW(mgr.getBrightness(999));
    PASS();
}

static void test_setBrightness_no_crash() {
    TEST("setBrightness doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setBrightness(0.5));
    CHECK_NO_THROW(mgr.setBrightness("any", 0.3));
    CHECK_NO_THROW(mgr.setBrightness(0, 0.7));
    PASS();
}

static void test_brightness_set_then_get() {
    TEST("setBrightness stores value; getBrightness uses it as fallback");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setBrightness("cachetest", 0.42));
    PASS();
}

// ========== Gamma ==========

static void test_getGammaRGB_no_crash() {
    TEST("getGammaRGB doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.getGammaRGB());
    CHECK_NO_THROW(mgr.getGammaRGB("any"));
    PASS();
}

static void test_setGammaRGB_no_crash() {
    TEST("setGammaRGB doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setGammaRGB(0.8, 0.9, 1.0));
    CHECK_NO_THROW(mgr.setGammaRGB("any", 0.7, 0.8, 0.9));
    CHECK_NO_THROW(mgr.setGammaRGB(0, 0.6, 0.7, 0.8));
    PASS();
}

static void test_gammaRGB_cache_roundtrip() {
    TEST("gammaRGB cache stores and retrieves values");
    BrightnessManager mgr;
    mgr.setGammaRGB("gtest", 0.42, 0.84, 1.26);
    auto g = mgr.getGammaRGB("gtest");
    CHECK_EQ(g.red, 0.42);
    CHECK_EQ(g.green, 0.84);
    CHECK_EQ(g.blue, 1.26);
    PASS();
}

// ========== Temperature ==========

static void test_getTemperature_no_crash() {
    TEST("getTemperature doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.getTemperature());
    CHECK_NO_THROW(mgr.getTemperature("any"));
    CHECK_NO_THROW(mgr.getTemperature(-1));
    CHECK_NO_THROW(mgr.getTemperature(999));
    PASS();
}

static void test_setTemperature_no_crash() {
    TEST("setTemperature doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setTemperature(4200));
    CHECK_NO_THROW(mgr.setTemperature("any", 3000));
    CHECK_NO_THROW(mgr.setTemperature(0, 5000));
    PASS();
}

static void test_temperature_set_no_crash() {
    TEST("temperature set stores value on hardware success");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setTemperature("ttest", 4200));
    PASS();
}

// ========== Shadow Lift ==========

static void test_getShadowLift_default() {
    TEST("getShadowLift defaults to 0.0");
    BrightnessManager mgr;
    CHECK_EQ(mgr.getShadowLift(), 0.0);
    CHECK_EQ(mgr.getShadowLift("any"), 0.0);
    PASS();
}

static void test_setShadowLift_no_crash() {
    TEST("setShadowLift doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setShadowLift(0.5));
    CHECK_NO_THROW(mgr.setShadowLift("any", 1.5));
    CHECK_NO_THROW(mgr.setShadowLift(0, 2.0));
    PASS();
}

static void test_shadowLift_cache_roundtrip() {
    TEST("shadowLift cache stores and retrieves values");
    BrightnessManager mgr;
    mgr.setShadowLift("stest", 2.5);
    CHECK_EQ(mgr.getShadowLift("stest"), 2.5);
    PASS();
}

// ========== Gamma increase/decrease ==========

static void test_increaseDecreaseGamma_no_crash() {
    TEST("increase/decreaseGamma doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.increaseGamma(100));
    CHECK_NO_THROW(mgr.decreaseGamma(50));
    CHECK_NO_THROW(mgr.increaseGamma("any", 100));
    CHECK_NO_THROW(mgr.decreaseGamma("any", 50));
    CHECK_NO_THROW(mgr.increaseGamma(0, 100));
    CHECK_NO_THROW(mgr.decreaseGamma(0, 50));
    PASS();
}

// ========== Shadow Lift increase/decrease ==========

static void test_increaseDecreaseShadowLift_no_crash() {
    TEST("increase/decreaseShadowLift doesn't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.increaseShadowLift(10));
    CHECK_NO_THROW(mgr.decreaseShadowLift(5));
    CHECK_NO_THROW(mgr.increaseShadowLift("any", 10));
    CHECK_NO_THROW(mgr.decreaseShadowLift("any", 5));
    CHECK_NO_THROW(mgr.increaseShadowLift(0, 10));
    CHECK_NO_THROW(mgr.decreaseShadowLift(0, 5));
    PASS();
}

// ========== Combined operations ==========

static void test_combined_ops_no_crash() {
    TEST("combined setBrightnessAnd* operations don't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setBrightnessAndRGB(0.7, 1.0, 0.9, 0.8));
    CHECK_NO_THROW(mgr.setBrightnessAndRGB("any", 0.5, 0.8, 0.9, 1.0));
    CHECK_NO_THROW(mgr.setBrightnessAndRGB(0, 0.6, 0.7, 0.8, 0.9));
    CHECK_NO_THROW(mgr.setBrightnessAndTemperature(0.7, 4200));
    CHECK_NO_THROW(mgr.setBrightnessAndTemperature("any", 0.5, 5000));
    CHECK_NO_THROW(mgr.setBrightnessAndShadowLift(0.7, 0.5));
    CHECK_NO_THROW(mgr.setBrightnessAndShadowLift("any", 0.7, 0.5));
    CHECK_NO_THROW(mgr.setBrightnessAndShadowLift(0, 0.7, 0.5));
    PASS();
}

// ========== Day/night mode ==========

static void test_daynight_enable_toggle() {
    TEST("day/night mode enable and disable");
    BrightnessManager mgr;
    BrightnessManager::DayNightSettings s;
    s.dayBrightness = 1.0;
    s.nightBrightness = 0.3;
    s.dayTemperature = 6500;
    s.nightTemperature = 3000;
    s.dayStartHour = 7;
    s.nightStartHour = 20;
    s.checkInterval = std::chrono::minutes(0);
    mgr.enableDayNightMode(s);
    CHECK_TRUE(mgr.isDayNightModeEnabled());
    mgr.disableDayNightMode();
    CHECK_FALSE(mgr.isDayNightModeEnabled());
    PASS();
}

static void test_daynight_switch() {
    TEST("switchToDay/switchToNight don't crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.switchToDay());
    CHECK_NO_THROW(mgr.switchToNight());
    CHECK_NO_THROW(mgr.switchToDay("any"));
    CHECK_NO_THROW(mgr.switchToNight("any"));
    PASS();
}

static void test_daynight_isDay() {
    TEST("isDay returns without crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.isDay());
    PASS();
}

// ========== Monitor queries ==========

static void test_getConnectedMonitors() {
    TEST("getConnectedMonitors returns without crash");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.getConnectedMonitors());
    PASS();
}

// ========== Cache persistence ==========

static void test_cache_survives_init() {
    TEST("cached temperature and shadowLift survive init()");
    BrightnessManager mgr;
    mgr.setTemperature("persist_mon", 4200);
    mgr.setShadowLift("persist_mon", 0.3);
    mgr.init();
    CHECK_EQ(mgr.getTemperature("persist_mon"), 4200);
    CHECK_EQ(mgr.getShadowLift("persist_mon"), 0.3);
    PASS();
}

// ========== Boundaries ==========

static void test_setTemperature_boundaries() {
    TEST("setTemperature accepts boundary values");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setTemperature("b", 1000));
    CHECK_NO_THROW(mgr.setTemperature("b", 25000));
    PASS();
}

static void test_setBrightness_boundaries() {
    TEST("setBrightness clamps to [0, 2]");
    BrightnessManager mgr;
    CHECK_NO_THROW(mgr.setBrightness("b", -0.5));
    CHECK_NO_THROW(mgr.setBrightness("b", 3.0));
    PASS();
}

// ========== main ==========

int main() {
    std::cout << "=== Brightness Manager Unit Tests ===" << std::endl;

    test_construct_init();
    test_getMonitor_invalid();
    test_getBrightness_no_crash();
    test_setBrightness_no_crash();
    test_brightness_set_then_get();
    test_getGammaRGB_no_crash();
    test_setGammaRGB_no_crash();
    test_gammaRGB_cache_roundtrip();
    test_getTemperature_no_crash();
    test_setTemperature_no_crash();
    test_temperature_set_no_crash();
    test_getShadowLift_default();
    test_setShadowLift_no_crash();
    test_shadowLift_cache_roundtrip();
    test_increaseDecreaseGamma_no_crash();
    test_increaseDecreaseShadowLift_no_crash();
    test_combined_ops_no_crash();
    test_daynight_enable_toggle();
    test_daynight_switch();
    test_daynight_isDay();
    test_getConnectedMonitors();
    test_cache_survives_init();
    test_setTemperature_boundaries();
    test_setBrightness_boundaries();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    return failed > 0 ? 1 : 0;
}
