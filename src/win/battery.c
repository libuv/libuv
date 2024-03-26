/* Copyright libuv project contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "winapi.h"

int uv__estimate_remaining_charging_time_in_seconds(SYSTEM_POWER_STATUS* sys_power_status) {
    int remaining;

    if (sys_power_status->ACLineStatus != 1) {
        remaining = -1;
        goto out;
    }

    if (sys_power_status->BatteryLifePercent == 100) {
        remaining = 0;
        goto out;
    }

    if (sys_power_status->BatteryLifeTime != -1 && sys_power_status->BatteryFullLifeTime != 1) {
        int rps = (sys_power_status->BatteryFullLifeTime - sys_power_status->BatteryLifeTime) / (100 - sys_power_status->BatteryLifePercent);
        remaining = rps * (100 - sys_power_status->BatteryLifePercent);
    }
    else {
        remaining = -1;
    }

out:
    return remaining;
}

int uv_battery_info(uv_battery_info_t* info) {
    SYSTEM_POWER_STATUS sys_power_status;
    int err;

    if (!GetSystemPowerStatus(&sys_power_status)) {
        err = /* Not sure... */1;
        goto out;
    }

    /* TODO: ACLineStatus is 255 if the status is Unknown, how to deal with it? */
    info->is_charging = sys_power_status.ACLineStatus == 1;
    /* TODO: BatteryLifePercent is 255 if the status is Unknown, how to deal with it? */
    info->level = (int)sys_power_status.BatteryLifePercent == 255 ? -1 : (int)sys_power_status.BatteryLifePercent;
    /* TODO: BatteryLifeTime is -1 if the status is Unknown, how to deal with it? */
    info->discharge_time_in_secs = sys_power_status.BatteryLifeTime;

    info->charge_time_in_secs = uv__estimate_remaining_charging_time_in_seconds(&sys_power_status);

    err = 0;

out:
    return err;
}
