#include "core/powerSave.h"
#include "utility/xpt2046.h"
#include <SPI.h>
#include <interface.h>
SPIClass *touchSPI = NULL;
XPT2046 touch;
void _setup_gpio() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    touchSPI = new SPIClass(HSPI);
    touchSPI->begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);
}
void _post_setup_gpio() {}
int getBattery() { return analogRead(BATTERY_PIN); }
void _setBrightness(uint8_t brightval) {
    if (brightval > 0) {
        digitalWrite(TFT_BL, HIGH);
    } else {
        digitalWrite(TFT_BL, LOW);
    }
}
bool getTouch(uint16_t *x, uint16_t *y) {
    if (touch.touched()) {
        TS_Point p = touch.getPoint();
        *x = p.x;
        *y = p.y;
        return true;
    }
    return false;
}
void InputHandler(void) {
    checkPowerSaveTime();
    PrevPress = false;
    NextPress = false;
    SelPress = false;
    AnyKeyPress = false;
    EscPress = false;
    touchPoint.pressed = false;

    if (touch.touched()) {
        if (!wakeUpScreen()) AnyKeyPress = true;
        else goto END;

        TS_Point t = touch.getPoint();
        touchPoint.x = t.x;
        touchPoint.y = t.y;
        touchPoint.pressed = true;
        touchHeatMap(touchPoint);
    }
END:
    if (AnyKeyPress) {
        long tmp = millis();
        while ((millis() - tmp) < 200 && touch.touched());
    }
}
String keyboard(String mytext, int maxSize, String msg) { return mytext; }
void powerOff() {}
void checkReboot() {}
