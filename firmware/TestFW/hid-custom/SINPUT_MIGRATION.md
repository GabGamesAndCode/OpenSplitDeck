# S-Input Protocol Migration - Complete

## Overview

Your OpenSplitDeck firmware has been successfully migrated from **Sony DualShock 4 (DS4)** protocol to **S-Input** protocol. S-Input is a modern, SDL-compatible HID gamepad protocol designed for better native game controller support.

## What Changed

### 1. **USB HID Protocol**
- **Old**: DS4 emulation (VID: 0x054C, PID: 0x05C4)
- **New**: S-Input protocol (VID: 0x2E8A, PID: 0x10C6)

### 2. **Report Format**
- **Old**: 64-byte DS4 reports with uint8 analog values (0-255)
- **New**: 64-byte S-Input reports with int16 analog values (-32768 to 32767, centered at 0)

### 3. **Button Mapping**
S-Input uses standardized SDL button names:
- Face buttons: `button_south` (A/X), `button_east` (B/○), `button_west` (X/□), `button_north` (Y/△)
- D-pad: Individual `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right` buttons
- Shoulders: `button_l_shoulder`/`button_r_shoulder` (L1/R1)
- Triggers: `button_l_trigger`/`button_r_trigger` (digital) + `trigger_l`/`trigger_r` (analog)
- System: `button_start`, `button_select`, `button_guide`, `button_share`
- Touchpad: `button_l_touchpad`, `button_r_touchpad`

### 4. **Haptics/Rumble**
- **Old**: 4-bit rumble packed into single byte (limited to 0-15)
- **New**: 8-bit stereo haptics (0-255 per motor) via S-Input Output Report 0x03
- Supports both Type 1 (frequency/amplitude) and Type 2 (ERM simple) haptics
- Haptics forwarded from USB to controllers via ESB ACK payloads

### 5. **IMU (Motion)**
- Same 6-axis IMU support (accelerometer + gyroscope)
- Now includes `imu_timestamp_us` field for better sync
- Values remain int16_t, same range

### 6. **Touchpad**
- Dual touchpad support maintained
- Coordinates changed from uint16 (0-1919) to int16 (centered at 0)
- Added pressure support: `touchpad_1_pressure`, `touchpad_2_pressure`

## Files Modified

### New Files
- `src/usb_hid_sinput.h` - S-Input protocol definitions and structures
- `src/usb_hid_sinput.c` - S-Input USB HID implementation

### Modified Files
- `src/main.c` - Updated to use S-Input reports instead of DS4
- `src/controller_esb.c` - Enhanced haptics support in ACK payloads
- `src/controller_esb.h` - Updated ACK structure for 8-bit haptics
- `CMakeLists.txt` - Updated source file list
- `prj.conf` - Updated USB VID/PID and product strings

### Obsolete Files (can be deleted)
- `src/usb_hid_composite.c` - Old DS4 implementation
- `src/usb_hid_composite.h` - Old DS4 headers

## Key Features

### ✅ Implemented
- [x] Full S-Input HID descriptor (official spec)
- [x] 64-byte input reports with all controller data
- [x] 32-button support (currently using ~20)
- [x] Dual analog sticks (int16 centered values)
- [x] Dual analog triggers (int16 centered values)
- [x] 6-axis IMU with timestamp
- [x] Dual touchpad with pressure
- [x] Stereo haptics/rumble (USB → ESB)
- [x] Plug status and battery reporting
- [x] SDL-compatible button mapping

### 🔧 Ready for Extension
- [ ] Player LED control (command 0x03)
- [ ] Joystick RGB control (command 0x04)
- [ ] Paddle buttons (hardware dependent)
- [ ] Feature response bytes for capability reporting

## Protocol Details

### Input Report 0x01 (64 bytes)
```c
typedef struct {
    uint8_t report_id;              // 0x01
    uint8_t plug_status;            // USB connection status
    uint8_t charge_percent;         // Battery 0-100
    uint8_t buttons_1;              // Face + D-pad
    uint8_t buttons_2;              // Shoulders + sticks
    uint8_t buttons_3;              // System buttons
    uint8_t buttons_4;              // Misc/power
    int16_t left_x;                 // -32768 to 32767
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    int16_t trigger_l;              // -32768 to 32767
    int16_t trigger_r;
    uint32_t imu_timestamp_us;      // Microseconds
    int16_t accel_x;                // Gs
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;                 // Degrees/sec
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t touchpad_1_x;           // Centered
    int16_t touchpad_1_y;
    int16_t touchpad_1_pressure;
    int16_t touchpad_2_x;
    int16_t touchpad_2_y;
    int16_t touchpad_2_pressure;
    uint8_t reserved_bulk[17];      // For command responses
} sinput_input_report_t;
```

### Output Report 0x03 (48 bytes)
Used for:
- **0x01**: Haptics (Type 1: frequency/amplitude, Type 2: ERM simple)
- **0x03**: Player LEDs
- **0x04**: Joystick RGB

### Haptics Flow
```
PC/Game → USB HID Output Report 0x03 (Command 0x01)
    ↓
usb_hid_sinput.c (set_report_cb)
    ↓
haptics_callback() in main.c
    ↓
controller_esb_set_haptics()
    ↓
ESB ACK payload (8 bytes: left_rumble, right_rumble)
    ↓
Controller receives and activates motors
```

## Button Mapping Reference

### Your Controller Layout → S-Input

**Left Controller:**
- Stick → `left_x`, `left_y`
- Trigger → `trigger_l`
- Bumper (L1) → `button_l_shoulder`
- Stick Click (L3) → `button_stick_left`
- Trackpad → `touchpad_2_x/y/pressure`
- Trackpad Click → `button_l_touchpad`
- D-pad → `dpad_up/down/left/right`
- Select → `button_select`

**Right Controller:**
- Stick → `right_x`, `right_y`
- Trigger → `trigger_r`
- Bumper (R1) → `button_r_shoulder`
- Stick Click (R3) → `button_stick_right`
- Trackpad → `touchpad_1_x/y/pressure`
- Trackpad Click → `button_r_touchpad`
- Face Buttons:
  - Button 0x01 (Y) → `button_north`
  - Button 0x02 (X) → `button_west`
  - Button 0x04 (B) → `button_east`
  - Button 0x08 (A) → `button_south`
- Start → `button_start`
- IMU → `accel_x/y/z`, `gyro_x/y/z`

**Either Controller:**
- Mode/Guide → `button_guide`

## Building

```bash
cd firmware/TestFW/hid-custom
west build -b xiao_ble_nrf52840 --pristine
west flash
```

## Testing

1. **USB Connection**: Device should enumerate as "S-Input Gamepad" (VID: 0x2E8A, PID: 0x10C6)

2. **SDL Detection**: 
   - Steam Input should detect natively
   - SDL games should recognize all buttons/axes
   - jstest (Linux): `jstest /dev/input/js0`

3. **Button Testing**:
   - All 32 button slots available
   - Test with `evtest` on Linux or game controller tester on Windows

4. **Haptics**:
   - Games with rumble support should trigger vibration
   - Check log output: "Haptics set: L=X, R=Y"

5. **IMU**:
   - Motion controls should work in supported games
   - Timestamp counter increments each report

## Advantages Over DS4

### S-Input Benefits:
1. **Native SDL support** - No mapping layer needed
2. **Higher precision analog** - 16-bit vs 8-bit values
3. **Simpler protocol** - Less authentication overhead
4. **Better haptics** - 256 levels vs 16 levels
5. **Touchpad pressure** - Added sensitivity data
6. **Open standard** - Community-driven, documented
7. **Future-proof** - Extensible design

### Trade-offs:
- No PS4/PS5 console compatibility (USB only for PC/Steam Deck)
- Simpler feature reports (no LED bar control like DS4)
- Generic VID/PID (can register custom if needed)

## Customization Options

### Change Face Style
In `usb_hid_sinput.c`, you could implement feature report 0x02 to return:
```c
uint8_t face_style = SINPUT_FACE_SONY;  // PlayStation symbols
// or
uint8_t face_style = SINPUT_FACE_ABXY;  // Xbox layout
```

### Custom VID/PID
To register your own device identity:
1. Update `usb_hid_sinput.c`: Change `0x2E8A` and `0x10C6`
2. Update `prj.conf`: Set new PID
3. Submit to SDL for native mapping

### Add Player LEDs
Implement LED control in `set_report_cb()` for command 0x03

### Add RGB Support
Implement RGB control in `set_report_cb()` for command 0x04

## Troubleshooting

### Device not recognized
- Check USB enumeration logs
- Verify VID/PID: `lsusb` (Linux) or Device Manager (Windows)
- Ensure HID descriptor compiled correctly

### Buttons not working
- Check button mapping in `main.c` → `process_controller_data()`
- Verify bit flags match your controller protocol
- Test with `evtest` to see raw button events

### Haptics not working
- Verify haptics callback registered in `main()`
- Check ESB ACK payload size (8 bytes)
- Controllers must implement rumble motor control

### IMU issues
- Timestamp should increment each report
- Check axis inversions (Y axes inverted)
- Verify int16 value ranges

## Next Steps

1. **Build and test** the firmware
2. **Verify USB enumeration** as S-Input device
3. **Test in SDL game** or Steam Input
4. **Implement optional features** (LEDs, RGB)
5. **Update controller firmware** to handle new 8-bit rumble values

## References

- [S-Input Official Documentation](https://docs.handheldlegend.com/s/sinput/)
- [SDL Gamepad Support](https://wiki.libsdl.org/SDL2/CategoryGameController)
- Original DS4 implementation: `src/usb_hid_composite.c` (archived)

---

**Migration completed**: All core functionality maintained, protocol upgraded to S-Input standard.
