#include "../logic.h"
#include <cstdio>

static int fails = 0;

static void check(bool ok, const char *msg) {
    if (ok) {
        printf("ok   %s\n", msg);
    } else {
        printf("FAIL %s\n", msg);
        fails++;
    }
}

int main() {
    lamp::ScheduleParams p{ 22, 0, 3600, 6, 0, 1800, 10, 2200, 255, 2700, 5000 };

    lamp::LampOutput o = lamp::schedule_output(12 * 3600, p);
    check(o.brightness == 255 && o.cct == 2700, "daytime: full brightness, default cct");

    o = lamp::schedule_output(6 * 3600, p);
    check(o.brightness == 10 && o.cct == 2200, "wake start: dim floor, warm");

    o = lamp::schedule_output(6 * 3600 + 900, p);
    check(o.brightness >= 220 && o.brightness <= 228, "wake mid: eased brightness");
    check(o.cct == 3600, "wake mid: half cct ramp");

    o = lamp::schedule_output(22 * 3600, p);
    check(o.brightness == 255, "bedtime start: full brightness");

    o = lamp::schedule_output(22 * 3600 + 1800, p);
    check(o.brightness >= 220 && o.brightness <= 228, "bedtime mid: eased dimming");

    o = lamp::schedule_output(23 * 3600, p);
    check(o.brightness == 10 && o.cct == 2200, "after bedtime: dim floor until wake");

    lamp::ScheduleParams p2{ 23, 0, 7200, 6, 0, 1800, 10, 2200, 255, 2700, 5000 };
    o = lamp::schedule_output(23 * 3600, p2);
    check(o.brightness == 255, "midnight-crossing: ramp starts at full");

    o = lamp::schedule_output(1 * 3600, p2);
    check(o.brightness == 10, "midnight-crossing: floor after 2h ramp");

    check(lamp::schedule_window_active(12 * 3600, p), "window active during day");
    check(!lamp::schedule_window_active(23 * 3600, p), "window inactive overnight");

    uint8_t r, g, b;
    lamp::kelvin_rgb(2700, &r, &g, &b);
    check(r == 255 && g > 140 && g < 200 && b < 100, "kelvin 2700K is warm");

    lamp::kelvin_rgb(5000, &r, &g, &b);
    check(r > 240 && g > 200 && b > 150, "kelvin 5000K is cool");

    if (fails) {
        printf("\n%d FAILURES\n", fails);
    } else {
        printf("\nall tests passed\n");
    }
    return fails ? 1 : 0;
}
