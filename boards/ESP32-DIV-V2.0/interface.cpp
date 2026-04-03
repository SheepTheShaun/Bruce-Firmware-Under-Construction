#include "core/powerSave.h"
#include "core/utils.h"
#include <SPI.h>
#include <interface.h>

#define DIV_DEBUG 1

#if DIV_DEBUG
#define DIV_LOG(x) Serial.println(String("[DIV_BOARD_DEBUG] ") + x)
#define DIV_LOGF(f, ...) Serial.printf("[DIV_BOARD_DEBUG] " f "\n", ##__VA_ARGS__)
#else
#define DIV_LOG(x)                                                                                           \
    do {                                                                                                     \
    } while (0)
#define DIV_LOGF(f, ...)                                                                                     \
    do {                                                                                                     \
    } while (0)
#endif

// We use TFT_eSPI's touch handling now (USE_TFT_eSPI_TOUCH) to avoid SPI collisions!

// --- Hardware Conflict Management ---
// GPIO2: Buzzer PWM vs Battery ADC
// GPIO14: NRF24 U3 CE vs IR TX
// GPIO21: NRF24 U3 CSN vs IR RX
bool hw_buzzer_active = false;
bool hw_nrf_u3_active = true; // Default state
bool hw_ir_active = false;

void acquireBuzzer() { hw_buzzer_active = true; }
void releaseBuzzer() { hw_buzzer_active = false; }

void acquireNRF24_U3() {
    hw_ir_active = false;
    hw_nrf_u3_active = true;
    pinMode(14, OUTPUT);
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH); // CSN high to deselect
    DIV_LOG("Acquired NRF24 U3 (overriding IR)");
}

void acquireIR() {
    hw_nrf_u3_active = false;
    hw_ir_active = true;
    pinMode(14, OUTPUT); // IR TX
    pinMode(21, INPUT);  // IR RX
    DIV_LOG("Acquired IR (overriding NRF24 U3)");
}
// -------------------------------------

void _setup_gpio() {
    DIV_LOG("Initializing _setup_gpio()...");
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    DIV_LOG("TFT Backlight initialized and turned ON.");

    DIV_LOG("TFT and Touch use the exact same hardware SPI bus.");
    DIV_LOGF(
        "Shared SPI Pins -> SCK: %d, MISO: %d, MOSI: %d, Touch CS: %d", TFT_SCLK, TFT_MISO, TFT_MOSI, TOUCH_CS
    );

    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    // Initialize 3x NRF24 modules
    // U1: CE=15, CSN=4
    pinMode(15, OUTPUT);
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);

    // U2: CE=47, CSN=48
    pinMode(47, OUTPUT);
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);

    // U3: CE=14, CSN=21 (Shared with IR)
    pinMode(14, OUTPUT);
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    pinMode(CC1101_CS_PIN, OUTPUT);
    pinMode(SDCARD_CS, OUTPUT);

    digitalWrite(CC1101_CS_PIN, HIGH);
    digitalWrite(SDCARD_CS, HIGH);

    bruceConfig.colorInverted = 0;
    DIV_LOG("_setup_gpio() completed successfully. Hardware initialized.");
}

void _post_setup_gpio() {
    DIV_LOG("_post_setup_gpio() called.");

#ifdef USE_TFT_eSPI_TOUCH
    DIV_LOG("Initializing built-in touch handling via TFT_eSPI.");
    pinMode(TOUCH_CS, OUTPUT);

    // Provide some default calibration data in case it isn't set, TFT_eSPI can self-calibrate
    uint16_t calData[5];

    // Ensure LittleFS is mounted before trying to load calibration
    if (!LittleFS.begin()) {
        DIV_LOG("LittleFS failed to mount properly in _post_setup_gpio()! Calibration might fail to save.");
        // Non-fatal, tft.setTouch(calData) might not run but we try.
    }

    File caldata = LittleFS.open("/calData", "r");

    if (!caldata) {
        DIV_LOG("No touch calibration data found! Triggering self-calibration UI...");
        tft.setRotation(ROTATION);
        tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 10);

        caldata = LittleFS.open("/calData", "w");
        if (caldata) {
            caldata.printf(
                "%d\n%d\n%d\n%d\n%d\n", calData[0], calData[1], calData[2], calData[3], calData[4]
            );
            caldata.close();
            DIV_LOG("Touch calibration complete and saved.");
        } else {
            DIV_LOG("Failed to save touch calibration to LittleFS!");
        }
    } else {
        DIV_LOG("Found touch calibration data:");
        for (int i = 0; i < 5; i++) {
            String line = caldata.readStringUntil('\n');
            calData[i] = line.toInt();
            DIV_LOGF("CalData[%d] = %d", i, calData[i]);
        }
        caldata.close();
    }
    tft.setTouch(calData);
    DIV_LOG("TFT_eSPI Touch configured and ready.");
#endif
}

int getBattery() {
    if (hw_buzzer_active) {
        DIV_LOG(
            "getBattery(): Cannot read battery! GPIO2 is currently in use by the Buzzer PWM (Hardware "
            "Conflict)"
        );
        return -1; // Indicate unavailable
    }
    int val = analogRead(BATTERY_PIN);
    DIV_LOGF("getBattery(): Read analog value %d from BATTERY_PIN (%d)", val, BATTERY_PIN);
    return val;
}

bool isCharging() {
    DIV_LOG("isCharging() returning false (no hardware definition).");
    return false;
}

void _setBrightness(uint8_t brightval) {
    DIV_LOGF("_setBrightness() called with value %d", brightval);
    if (brightval > 0) {
        digitalWrite(TFT_BL, HIGH);
        // DIV_LOG("TFT Backlight set to HIGH.");
    } else {
        digitalWrite(TFT_BL, LOW);
        // DIV_LOG("TFT Backlight set to LOW.");
    }
}

bool getTouch(uint16_t *x, uint16_t *y) {
#ifdef USE_TFT_eSPI_TOUCH
    bool touched = tft.getTouch(x, y);
    if (touched) { DIV_LOGF("getTouch(): Touch detected at x=%d, y=%d", *x, *y); }
    return touched;
#else
    return false;
#endif
}

void InputHandler(void) {
    checkPowerSaveTime();

    // Clear legacy single presses
    PrevPress = false;
    NextPress = false;
    SelPress = false;
    AnyKeyPress = false;
    EscPress = false;
    touchPoint.pressed = false;

    static unsigned long lastTouchTime = 0;

#ifdef USE_TFT_eSPI_TOUCH
    TouchPoint t;
    bool touched = tft.getTouch(&t.x, &t.y);
    if (touched && millis() - lastTouchTime > 200) {
        lastTouchTime = millis();
        DIV_LOGF("InputHandler(): Touch input handled at raw x=%d, y=%d", t.x, t.y);

        // Map touch points according to screen rotation
        if (bruceConfigPins.rotation == 3) {
            t.y = (tftHeight + 20) - t.y;
            t.x = tftWidth - t.x;
        } else if (bruceConfigPins.rotation == 0) {
            uint16_t tmp = t.x;
            t.x = map((tftHeight + 20) - t.y, 0, 320, 0, 240);
            t.y = map(tmp, 0, 240, 0, 320);
        } else if (bruceConfigPins.rotation == 2) {
            uint16_t tmp = t.x;
            t.x = map(t.y, 0, 320, 0, 240);
            t.y = map(tftWidth - tmp, 0, 240, 0, 320);
        }
        DIV_LOGF("InputHandler(): Mapped input to x=%d, y=%d (rot=%d)", t.x, t.y, bruceConfigPins.rotation);

        if (!wakeUpScreen()) {
            AnyKeyPress = true;
            DIV_LOG("InputHandler(): Screen awakened, setting AnyKeyPress = true.");
            return;
        }

        // Apply touch globally
        touchPoint.x = t.x;
        touchPoint.y = t.y;
        touchPoint.pressed = true;
        touchHeatMap(touchPoint);
    }
#endif
}

void powerOff() { DIV_LOG("powerOff() stub called."); }

void checkReboot() { DIV_LOG("checkReboot() stub called."); }
