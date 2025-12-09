#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include "controller_esb.h"
#include "usb_hid_sinput.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

// Haptics callback - forwards S-Input haptics to ESB controllers
static void haptics_callback(uint8_t left_amp, uint8_t right_amp)
{
    controller_esb_set_haptics(left_amp, right_amp);
}

// Helper function to map a value from one range to another
long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Low-pass filter for gyro data (removes high-frequency noise)
static int16_t gyro_lowpass_filter(int16_t new_value, int16_t prev_value, float alpha)
{
    // alpha = 0.0 to 1.0, lower values = more filtering (smoother but slower response)
    // Typical values: 0.1 to 0.3 for good filtering
    return (int16_t)(alpha * new_value + (1.0f - alpha) * prev_value);
}

// Apply deadzone to joystick input (removes drift at center)
static int16_t apply_joystick_deadzone(int8_t value, int8_t deadzone)
{
    if (value > -deadzone && value < deadzone) {
        return 0;  // Within deadzone - snap to center
    }
    // Scale from int8_t to int16_t
    return (int16_t)value * 256;
}

// Convert controller data to S-Input HID reports (using separated controller states)
static void process_controller_data(const struct device *hid_dev)
{
    uint32_t func_start = k_uptime_get_32();
    
    // Get SEPARATE controller states - no more shared state corruption!
    simple_controller_state_t *left_controller = controller_esb_get_left_state();
    simple_controller_state_t *right_controller = controller_esb_get_right_state();

    // DEBUG: Log if we have any controller data
    static uint32_t last_debug_log = 0;
    uint32_t now = k_uptime_get_32();
    if (now - last_debug_log > 1000) { // Log every second
        LOG_INF("L_recv:%d R_recv:%d", left_controller->data_received, right_controller->data_received);
        if (left_controller->data_received) {
            LOG_INF("L: btns:0x%02X flags:0x%02X stick:%d,%d trig:%d pad:%d,%d", 
                    left_controller->buttons, left_controller->flags,
                    left_controller->stickX, left_controller->stickY,
                    left_controller->trigger, left_controller->padX, left_controller->padY);
        }
        if (right_controller->data_received) {
            LOG_INF("R: btns:0x%02X flags:0x%02X stick:%d,%d trig:%d pad:%d,%d",
                    right_controller->buttons, right_controller->flags,
                    right_controller->stickX, right_controller->stickY,
                    right_controller->trigger, right_controller->padX, right_controller->padY);
        }
        last_debug_log = now;
    }

    // Static S-Input report structure - initialized to zero on first call
    static sinput_input_report_t report;
    static bool report_initialized = false;
    
    if (!report_initialized) {
        memset(&report, 0, sizeof(report));
        report.report_id = SINPUT_INPUT_REPORT_ID;
        report.plug_status = 0x01;  // Bit 0: USB connected
        report.charge_percent = 100;  // Always full when USB powered
        report_initialized = true;
    }
    
    // Static variables for touchpad smoothing with multi-frame interpolation
    static uint16_t raw_touch1_x = 0, raw_touch1_y = 0;
    static uint16_t raw_touch2_x = 0, raw_touch2_y = 0;
    static uint16_t last_touch1_x = 0, last_touch1_y = 0;
    static uint16_t last_touch2_x = 0, last_touch2_y = 0;
    static bool last_touch1_active = false;
    static bool last_touch2_active = false;
    
    // Multi-frame smoothing for touchpad (even smoother than before)
    static int16_t smooth_touchpad_x = 0, smooth_touchpad_y = 0;
    static bool was_touching = false;  // Track if we were touching last frame
    static const float touchpad_alpha = 0.2f; // Very heavy smoothing (0.1-0.2 for touchpads)
    
    // Static variables for gyro low-pass filtering
    static int16_t prev_gyro_x = 0, prev_gyro_y = 0, prev_gyro_z = 0;
    static const float gyro_alpha = 0.08f; // Lower = more filtering, less noise (0.1-0.3 typical)
    
    // Static variables for accelerometer low-pass filtering
    static int16_t prev_accel_x = 0, prev_accel_y = 0, prev_accel_z = 0;
    static const float accel_alpha = 0.5f; // Accel can be slightly faster response than gyro
    
    // Temporary variables for touchpad state within this function
    bool touch1_active = false;
    bool touch2_active = false;
    
    // Clear touchpad button (will be set if either controller presses it)
    report.button_touchpad_1 = 0;
    
    // Process LEFT controller data independently
    if (left_controller->data_received)
    {
        // Left analog stick (convert int8_t -128 to 127 → int16_t -32768 to 32767, with deadzone)
        report.left_x = apply_joystick_deadzone(left_controller->stickX, 5);
        report.left_y = apply_joystick_deadzone(left_controller->stickY, 5);
        
        // Left trigger (convert uint8_t 0-255 → int16_t -32768 to 32512, centered at 0)
        // Map: 0 → -32768 (not pressed), 128 → 0 (half), 255 → 32512 (full)
        report.trigger_l = ((int16_t)left_controller->trigger - 128) * 256;

        // Left touchpad (map to left half of single touchpad: 0-479 X range)
        touch2_active = (left_controller->padX != 0 || left_controller->padY != 0);
        raw_touch2_x = touch2_active ? map(left_controller->padY, 0, 1023, 479, 0) : 0;  // Inverted X
        raw_touch2_y = touch2_active ? map(left_controller->padX, 0, 1023, 0, 942) : 0;

        // D-pad buttons (S-Input format - individual bits, NOT encoded)
        report.dpad_up = (left_controller->buttons & 0x01) ? 1 : 0;
        report.dpad_down = (left_controller->buttons & 0x08) ? 1 : 0;
        report.dpad_left = (left_controller->buttons & 0x02) ? 1 : 0;
        report.dpad_right = (left_controller->buttons & 0x04) ? 1 : 0;

        // Left controller buttons
        report.button_l_shoulder = (left_controller->buttons & 0x10) ? 1 : 0;
        report.button_l_trigger = (left_controller->trigger > 128) ? 1 : 0;
        report.button_stick_left = (left_controller->buttons & 0x20) ? 1 : 0;
        
        // Left touchpad click -> touchpad_1 button (combined, only 1 physical touchpad)
        if (left_controller->buttons & 0x40) {
            report.button_touchpad_1 = 1;
            LOG_DBG("Left touchpad clicked");
        }
        
        report.button_select = (left_controller->buttons & 0x80) ? 1 : 0;
        report.button_guide = (left_controller->flags & 0x40) ? 1 : 0;
        
        // Left back paddles (B4, B5 flags)
        report.button_l_paddle_1 = (left_controller->flags & 0x01) ? 1 : 0;  // B4
        report.button_l_paddle_2 = (left_controller->flags & 0x02) ? 1 : 0;  // B5
    }

    // Process RIGHT controller data independently
    if (right_controller->data_received)
    {
        // Right analog stick (convert int8_t -128 to 127 → int16_t -32768 to 32767, with deadzone)
        report.right_x = apply_joystick_deadzone(right_controller->stickX, 5);
        report.right_y = apply_joystick_deadzone(right_controller->stickY, 5);
        
        // Right trigger (convert uint8_t 0-255 → int16_t -32768 to 32512, centered at 0)
        // Map: 0 → -32768 (not pressed), 128 → 0 (half), 255 → 32512 (full)
        report.trigger_r = ((int16_t)right_controller->trigger - 128) * 256;

        // IMU from right controller with low-pass filtering
        // Scale accelerometer down - raw values seem to be in a higher range than expected
        // Divide by 4 to bring into reasonable range (adjust if needed)
        int16_t raw_accel_x = right_controller->accelX / 800;
        int16_t raw_accel_y = right_controller->accelZ / 800;
        int16_t raw_accel_z = right_controller->accelY / 800;
        
        report.accel_x = gyro_lowpass_filter(raw_accel_x, prev_accel_x, accel_alpha);
        report.accel_y = gyro_lowpass_filter(raw_accel_y, prev_accel_y, accel_alpha);
        report.accel_z = gyro_lowpass_filter(raw_accel_z, prev_accel_z, accel_alpha);
        
        prev_accel_x = report.accel_x;
        prev_accel_y = report.accel_y;
        prev_accel_z = report.accel_z;
        
        // Gyro with low-pass filtering to reduce noise
        // Controller axes: gyroX=forward/back, gyroY=steering, gyroZ=left/right tilt
        // Report axes: gyro_x=pitch(forward/back), gyro_y=yaw(steering), gyro_z=roll(left/right)
        int16_t raw_gyro_x = right_controller->gyroX * -5; // forward/back (inverted)
        int16_t raw_gyro_y = right_controller->gyroZ * 5; // left/right tilt
        int16_t raw_gyro_z = right_controller->gyroY * -5; // steer
        
        report.gyro_x = gyro_lowpass_filter(raw_gyro_x, prev_gyro_x, gyro_alpha);
        report.gyro_y = gyro_lowpass_filter(raw_gyro_y, prev_gyro_y, gyro_alpha);
        report.gyro_z = gyro_lowpass_filter(raw_gyro_z, prev_gyro_z, gyro_alpha);
        
        prev_gyro_x = report.gyro_x;
        prev_gyro_y = report.gyro_y;
        prev_gyro_z = report.gyro_z;

        // Right touchpad (map to right half of single touchpad: 480-959 X range)
        touch1_active = (right_controller->padX != 0 || right_controller->padY != 0);
        raw_touch1_x = touch1_active ? map(right_controller->padY, 0, 1023, 480, 959) : 0;
        raw_touch1_y = touch1_active ? map(right_controller->padX, 0, 1023, 942, 0) : 0;  // Inverted Y

        // Right controller face buttons
        report.button_north = (right_controller->buttons & 0x01) ? 1 : 0;
        report.button_west = (right_controller->buttons & 0x02) ? 1 : 0;
        report.button_east = (right_controller->buttons & 0x04) ? 1 : 0;
        report.button_south = (right_controller->buttons & 0x08) ? 1 : 0;
        
        // Right controller buttons
        report.button_r_shoulder = (right_controller->buttons & 0x10) ? 1 : 0;
        report.button_r_trigger = (right_controller->trigger > 128) ? 1 : 0;
        report.button_stick_right = (right_controller->buttons & 0x20) ? 1 : 0;
        
        // Right touchpad click -> touchpad_1 button (combined, only 1 physical touchpad)
        if (right_controller->buttons & 0x40) {
            report.button_touchpad_1 = 1;
            LOG_DBG("Right touchpad clicked");
        }
        
        report.button_start = (right_controller->buttons & 0x80) ? 1 : 0;
        
        // Right back paddles (B4, B5 flags)
        report.button_r_paddle_1 = (right_controller->flags & 0x01) ? 1 : 0;  // B4
        report.button_r_paddle_2 = (right_controller->flags & 0x02) ? 1 : 0;  // B5
        
        // Guide button can be set by either controller
        if (right_controller->flags & 0x40)
        {
            report.button_guide = 1;
        }
    }
    
    // Combine both touchpads into single touchpad report (outside controller blocks)
    // Recalculate from raw values with small deadzone to detect true "no touch"
    const uint16_t TOUCH_DEADZONE = 10;  // Ignore values below this threshold
    touch1_active = (raw_touch1_x > TOUCH_DEADZONE || raw_touch1_y > TOUCH_DEADZONE);
    touch2_active = (raw_touch2_x > TOUCH_DEADZONE || raw_touch2_y > TOUCH_DEADZONE);
    
    // Use whichever touchpad is active (right takes priority if both)
    uint16_t active_x = touch1_active ? raw_touch1_x : (touch2_active ? raw_touch2_x : 0);
    uint16_t active_y = touch1_active ? raw_touch1_y : (touch2_active ? raw_touch2_y : 0);
    
    if (touch1_active || touch2_active)
    {
        // Convert raw coordinates to centered int16 range
        int16_t target_x = (int16_t)(((int32_t)active_x - 479) * 65535 / 959);
        int16_t target_y = (int16_t)(((int32_t)active_y - 471) * 65535 / 942);
        
        // On first touch, snap to position immediately (no filter from 0)
        if (!was_touching) {
            smooth_touchpad_x = target_x;
            smooth_touchpad_y = target_y;
            was_touching = true;
        } else {
            // Apply heavy low-pass filter for super smooth tracking
            smooth_touchpad_x = gyro_lowpass_filter(target_x, smooth_touchpad_x, touchpad_alpha);
            smooth_touchpad_y = gyro_lowpass_filter(target_y, smooth_touchpad_y, touchpad_alpha);
        }
        
        report.touchpad_1_x = smooth_touchpad_x;
        report.touchpad_1_y = smooth_touchpad_y;
        report.touchpad_1_pressure = 255;
    }
    else
    {
        // No touch - immediately reset to zero (no gradual fade)
        smooth_touchpad_x = 0;
        smooth_touchpad_y = 0;
        was_touching = false;
        report.touchpad_1_x = 0;
        report.touchpad_1_y = 0;
        report.touchpad_1_pressure = 0;
    }
    
    // Update history
    last_touch1_x = raw_touch1_x;
    last_touch1_y = raw_touch1_y;
    last_touch1_active = touch1_active;
    last_touch2_x = raw_touch2_x;
    last_touch2_y = raw_touch2_y;
    last_touch2_active = touch2_active;
    
    // Clear touchpad 2 (not used in single-touchpad mode)
    report.touchpad_2_x = 0;
    report.touchpad_2_y = 0;
    report.touchpad_2_pressure = 0;

    // Send the S-Input report
    usb_hid_sinput_send_report(hid_dev, &report);
                                                  
    // Log function timing if it's slow
    uint32_t func_time = k_uptime_get_32() - func_start;
    if (func_time > 5) { // Only warn if function takes over 5ms (was 1ms)
        LOG_WRN("Slow process_controller_data: %dms", func_time);
    }
}

int main(void)
{
    const struct device *hid_dev;
    int ret;

    if (!gpio_is_ready_dt(&led0))
    {
        // LOG_ERR("LED device %s is not ready", led0.port->name);
        return 0;
    }

    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT);
    if (ret < 0)
    {
        // LOG_ERR("Failed to configure the LED pin, error: %d", ret);
        return 0;
    }

    // Initialize USB HID S-Input device
    ret = usb_hid_sinput_init();
    if (ret != 0)
    {
        // LOG_ERR("Failed to initialize USB HID");
        return 0;
    }

    // Get the HID device handle
    hid_dev = usb_hid_sinput_get_device();
    if (hid_dev == NULL)
    {
        // LOG_ERR("Failed to get USB HID device");
        return 0;
    }

    // Register haptics callback to forward haptics to controllers
    usb_hid_sinput_register_haptics_callback(haptics_callback);

    // Initialize ESB
    ret = controller_esb_init();
    if (ret != 0)
    {
        // LOG_ERR("Failed to initialize ESB");
        return 0;
    }

    // LOG_INF("Waiting for radio to fully initialize...");
    k_sleep(K_MSEC(500));
    // LOG_INF("Starting ESB ping loop");

    // Main loop - poll controllers and process responses
    uint32_t last_report_time = 0;

    while (true)
    {
        uint32_t now = k_uptime_get_32();
        uint32_t loop_start = now;

        if (now - last_report_time >= 4)
        { // 250Hz (4ms) - low latency USB reporting
            last_report_time = now;
        //     uint32_t process_start = k_uptime_get_32();
            process_controller_data(hid_dev);
        //     uint32_t process_end = k_uptime_get_32();
            
            // Log if processing takes too long
            // uint32_t process_time = process_end - process_start;
            // if (process_time > 5) { // Only warn if processing takes over 5ms (was 2ms)
            //     LOG_WRN("Long processing time: %dms", process_time);
            // }
        }

        // Log if entire loop iteration takes too long
        uint32_t loop_end = k_uptime_get_32();
        uint32_t loop_time = loop_end - loop_start;
        if (loop_time > 10) { // Only warn if loop takes over 10ms (was 3ms)
            LOG_WRN("Long loop time: %dms", loop_time);
        }

        // Small delay to prevent overwhelming the system - increased for better RF processing
        k_sleep(K_USEC(500));
    }
    return 0;
}
