#ifndef USB_HID_SINPUT_H
#define USB_HID_SINPUT_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/class/usb_hid.h>

// S-Input Report IDs
#define SINPUT_INPUT_REPORT_ID      0x01
#define SINPUT_COMMAND_REPORT_ID    0x02
#define SINPUT_OUTPUT_REPORT_ID     0x03

// S-Input Gamepad Physical Types (SDL compatibility)
typedef enum {
    SINPUT_TYPE_UNKNOWN = 0,
    SINPUT_TYPE_STANDARD = 1,
    SINPUT_TYPE_XBOX_360 = 2,
    SINPUT_TYPE_XBOX_ONE = 3,
    SINPUT_TYPE_PS3 = 4,
    SINPUT_TYPE_PS4 = 5,
    SINPUT_TYPE_PS5 = 6,
    SINPUT_TYPE_SWITCH_PRO = 7,
    SINPUT_TYPE_JOYCON_LEFT = 8,
    SINPUT_TYPE_JOYCON_RIGHT = 9,
    SINPUT_TYPE_JOYCON_PAIR = 10,
    SINPUT_TYPE_GAMECUBE = 11
} sinput_gamepad_type_t;

// S-Input Face Styles (SDL compatibility)
typedef enum {
    SINPUT_FACE_UNKNOWN = 0,  // Xbox default
    SINPUT_FACE_ABXY = 1,     // Xbox Style: A, B, X, Y
    SINPUT_FACE_AXBY = 2,     // GameCube Style: A, X, B, Y
    SINPUT_FACE_BAYX = 3,     // Nintendo Style: B, A, Y, X
    SINPUT_FACE_SONY = 4      // PS4 Style: X, O, □, △
} sinput_face_style_t;

// S-Input Input Report 0x01 (64 bytes)
#pragma pack(push, 1)
typedef struct {
    uint8_t report_id;          // 0: Report ID (0x01)
    uint8_t plug_status;        // 1: Plug Status (bit flags)
    uint8_t charge_percent;     // 2: Battery charge 0-100
    
    // Buttons (4 bytes total - S-Input official format)
    // Reference: https://docs.handheldlegend.com/s/sinput/doc/buttons-format-esVqGUAjpb
    union {
        struct {
            // Byte 0 (offset 3 in report)
            uint8_t button_south       : 1; // Bit 0: A / Cross
            uint8_t button_east        : 1; // Bit 1: B / Circle
            uint8_t button_west        : 1; // Bit 2: X / Square
            uint8_t button_north       : 1; // Bit 3: Y / Triangle
            uint8_t dpad_up            : 1; // Bit 4: D-pad Up
            uint8_t dpad_down          : 1; // Bit 5: D-pad Down
            uint8_t dpad_left          : 1; // Bit 6: D-pad Left
            uint8_t dpad_right         : 1; // Bit 7: D-pad Right
            
            // Byte 1 (offset 4 in report)
            uint8_t button_stick_left  : 1; // Bit 0: L3
            uint8_t button_stick_right : 1; // Bit 1: R3
            uint8_t button_l_shoulder  : 1; // Bit 2: L1
            uint8_t button_r_shoulder  : 1; // Bit 3: R1
            uint8_t button_l_trigger   : 1; // Bit 4: L2 digital
            uint8_t button_r_trigger   : 1; // Bit 5: R2 digital
            uint8_t button_l_paddle_1  : 1; // Bit 6: L Paddle 1
            uint8_t button_r_paddle_1  : 1; // Bit 7: R Paddle 1
            
            // Byte 2 (offset 5 in report)
            uint8_t button_start       : 1; // Bit 0: Plus/Options
            uint8_t button_select      : 1; // Bit 1: Minus/Share
            uint8_t button_guide       : 1; // Bit 2: Home/PS
            uint8_t button_capture     : 1; // Bit 3: Capture/Misc1
            uint8_t button_l_paddle_2  : 1; // Bit 4: L Paddle 2
            uint8_t button_r_paddle_2  : 1; // Bit 5: R Paddle 2
            uint8_t button_touchpad_1  : 1; // Bit 6: Touchpad Button 1
            uint8_t button_touchpad_2  : 1; // Bit 7: Touchpad Button 2 (Misc 2)
            
            // Byte 3 (offset 6 in report)
            uint8_t button_power       : 1; // Bit 0: Misc 3 (Power)
            uint8_t button_misc_4      : 1; // Bit 1: Misc 4
            uint8_t button_misc_5      : 1; // Bit 2: Misc 5
            uint8_t button_misc_6      : 1; // Bit 3: Misc 6
            uint8_t button_misc_7      : 1; // Bit 4: Unused Misc 7
            uint8_t button_misc_8      : 1; // Bit 5: Unused Misc 8
            uint8_t button_misc_9      : 1; // Bit 6: Unused Misc 9
            uint8_t button_misc_10     : 1; // Bit 7: Unused Misc 10
        };
        struct {
            uint8_t buttons_1;          // 3: Byte 0 of buttons
            uint8_t buttons_2;          // 4: Byte 1 of buttons
            uint8_t buttons_3;          // 5: Byte 2 of buttons
            uint8_t buttons_4;          // 6: Byte 3 of buttons
        };
    };
    
    // Analog inputs (int16, centered at 0)
    int16_t left_x;             // 7-8: Left stick X
    int16_t left_y;             // 9-10: Left stick Y
    int16_t right_x;            // 11-12: Right stick X
    int16_t right_y;            // 13-14: Right stick Y
    int16_t trigger_l;          // 15-16: Left trigger analog (-32768 to 32767, centered at 0)
    int16_t trigger_r;          // 17-18: Right trigger analog (-32768 to 32767, centered at 0)
    
    // IMU data
    uint32_t imu_timestamp_us;  // 19-22: IMU timestamp in microseconds
    int16_t accel_x;            // 23-24: Accelerometer X (Gs)
    int16_t accel_y;            // 25-26: Accelerometer Y
    int16_t accel_z;            // 27-28: Accelerometer Z
    int16_t gyro_x;             // 29-30: Gyroscope X (DPS)
    int16_t gyro_y;             // 31-32: Gyroscope Y
    int16_t gyro_z;             // 33-34: Gyroscope Z
    
    // Touchpad data (X, Y, pressure - single touchpad, app splits in half)
    int16_t touchpad_1_x;       // 35-36: Touchpad 1 X (right controller)
    int16_t touchpad_1_y;       // 37-38: Touchpad 1 Y (right controller)
    int16_t touchpad_1_pressure;// 39-40: Touchpad 1 pressure
    int16_t touchpad_2_x;       // 41-42: Touchpad 2 X (left controller)
    int16_t touchpad_2_y;       // 43-44: Touchpad 2 Y (left controller)
    int16_t touchpad_2_pressure;// 45-46: Touchpad 2 pressure
    
    uint8_t reserved_bulk[17];  // 47-63: Reserved for command response data
} sinput_input_report_t;
#pragma pack(pop)

// S-Input Haptics Type 1 - Precise Stereo Haptics (frequency/amplitude pairs)
#pragma pack(push, 1)
typedef struct {
    uint8_t command_id;         // 0x01
    uint8_t type;               // 0x01
    
    struct {
        uint16_t frequency_1;   // Target: 40-2000 Hz
        uint16_t amplitude_1;
        uint16_t frequency_2;
        uint16_t amplitude_2;
    } left;
    
    struct {
        uint16_t frequency_1;
        uint16_t amplitude_1;
        uint16_t frequency_2;
        uint16_t amplitude_2;
    } right;
} sinput_haptic_type1_t;
#pragma pack(pop)

// S-Input Haptics Type 2 - ERM Stereo Haptics (simple amplitude + brake)
#pragma pack(push, 1)
typedef struct {
    uint8_t command_id;         // 0x01
    uint8_t type;               // 0x02
    
    struct {
        uint8_t amplitude;
        bool brake;
    } left;
    
    struct {
        uint8_t amplitude;
        bool brake;
    } right;
} sinput_haptic_type2_t;
#pragma pack(pop)

// Initialize S-Input USB HID device
int usb_hid_sinput_init(void);

// Get the HID device handle
const struct device* usb_hid_sinput_get_device(void);

// Send S-Input gamepad report
int usb_hid_sinput_send_report(const struct device *hid_dev, const sinput_input_report_t *report);

// Register haptics callback (called when host sends haptics commands)
void usb_hid_sinput_register_haptics_callback(void (*callback)(uint8_t left_amp, uint8_t right_amp));

#endif // USB_HID_SINPUT_H
