    #pragma once

    #include <string>
    #include <tuple>
    #include <cstdint>
    #include <optional>

    namespace havel {

    class BrightnessManager {
    public:
        struct Settings {
            double dayBrightness;
            double nightBrightness;
            int dayTemperature;
            int nightTemperature;
            std::string latitude;
            std::string longitude;
            bool verbose;

            // Current state
            double currentBrightness;
            int currentGamma;
        };

        // Constants for default values
        static constexpr double DEFAULT_BRIGHTNESS = 0.85;
        static constexpr double STARTUP_BRIGHTNESS = 0.3;
        static constexpr int STARTUP_GAMMA = 7500; // 75% of 10000K
        double brightnessAmount = 0.02;
        int gammaAmount = 200;
        
        BrightnessManager();
        ~BrightnessManager() = default;

        // Main functions
        bool setBrightnessAndTemperature(const std::string& brightness, const std::string& gamma);
        bool resetToDefaults();
        bool setStartupValues();
        bool setDefaultBrightness();

        // Incremental adjustments
        static constexpr double DEFAULT_BRIGHTNESS_AMOUNT = 0.02;
        static constexpr int DEFAULT_GAMMA_AMOUNT = 200;

        bool increaseBrightness(double amount = DEFAULT_BRIGHTNESS_AMOUNT);
        bool decreaseBrightness(double amount = DEFAULT_BRIGHTNESS_AMOUNT);
        bool increaseGamma(int amount = DEFAULT_GAMMA_AMOUNT);
        bool decreaseGamma(int amount = DEFAULT_GAMMA_AMOUNT);

        // Setters
        void setVerbose(bool verbose) { settings.verbose = verbose; }
        void setLocation(const std::string& lat, const std::string& lon);

        // Getters
        std::optional<double> getCurrentBrightness();
        double getCurrentBrightnessValue() const { return settings.currentBrightness; }
        int getCurrentGamma() const { return settings.currentGamma; }

    private:
        // Helper functions
        bool validateBrightness(const std::string& brightness);
        bool validateGamma(const std::string& gamma);
        int convertGammaToTemperature(int gamma);
        std::tuple<double, double> parseBrightnessValue(const std::string& brightness);
        std::tuple<int, int> parseGammaValue(const std::string& gamma);
        bool isX11();
        bool adjustBrightnessRandr(double& dayBrightness, double& nightBrightness);
        bool adjustBrightnessWayland(double& dayBrightness, double& nightBrightness);
        bool executeGammastep();

        Settings settings;
        const int MIN_TEMPERATURE = 1000;
        const int MAX_TEMPERATURE = 25000;
        std::string displayMethod; // "randr" or "wayland"
    };

    } // namespace havel