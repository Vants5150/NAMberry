//
// config.h - Pisound v1.2 + Wi-Fi test on Circle (Raspberry Pi 5)
//
#ifndef _config_h
#define _config_h

// sampling rate (48000, 96000 or 192000: the Pisound OSR pins are set accordingly)
#define SAMPLE_RATE		48000
// size of the DMA block in frames (samples per channel): the loop latency is 2 x CHUNK_FRAMES + converters
#define CHUNK_FRAMES		4
#define CHUNK_SIZE		(CHUNK_FRAMES * 2)		// in words (L and R)
#define QUEUE_SIZE_MSECS	100
#define TONE_LEVEL		0.2f

// SAFE MODE: after BOOT_MAX_FAILS consecutive boots without 30 s of stable operation
// (hang or watchdog reboot), the Pi starts with this configuration and stays reachable over Wi-Fi
#define SAFE_SAMPLE_RATE	48000
#define SAFE_CHUNK_FRAMES	128
#define BOOT_MAX_FAILS		3
// processor and fan: at startup the board runs at 1500 MHz, the maximum is 2400. From the page (Functions > Temperature and fan) you can choose the maximum frequency
// at the next boot and the thresholds of the active heatsink fan (GPIO 45, low level = on). Defaults: normal frequency, fan managed by the system.
#define FAN_GPIO		45
#define FAN_ON_DEFAULT_C	55
#define FAN_OFF_DEFAULT_C	47
// PWM fan: GPIO 45 = channel with index 3 of the RP1's PWM1, inverted polarity, about 24 kHz like Linux's pwm-fan driver. With "I control the fan" the speed follows the
// temperature: FAN_MIN_PCT at the turn-on threshold, up to the maximum chosen on the page (default FAN_MAX_DEFAULT_PCT, the level found bearable) at FAN_CAP_AT_C degrees,
// then it rises to 100% at FAN_FULL_C degrees (protection). Turn-off with hysteresis at the turn-off threshold.
// The "fandt" op (v = 0..100, -1 = end) sets the speed by hand: it ends by itself after FAN_PWM_TEST_SECONDS without new commands or at FAN_PWM_MAX_C degrees.
#define FAN_PWM_HZ		24000
#define FAN_PWM_RANGE		100
#define FAN_MIN_PCT		30
#define FAN_MAX_DEFAULT_PCT	55
#define FAN_CAP_AT_C		68
#define FAN_FULL_C		75
#define FAN_PWM_MAX_C		70
#define FAN_PWM_TEST_SECONDS	300
#define THROTTLE_HI_MC		82000
#define THROTTLE_LO_MC		72000
#define AP_IDLE_SECONDS		600		// in the fallback network, with no device connected after this many seconds: reboot to look for the home network again
#define STA_WAIT_SECONDS	60		// without an address after this many seconds from the home network: reboot into the fallback network (access point)
#define STABLE_SECONDS		30
#define BOOT_COUNTER_FILE	"SD:/boot.cnt"
#define WATCHDOG_SECONDS	15

// TEST_MODE 0: 440 Hz tone and level beep (only the ear is needed)
// TEST_MODE 1: latency measurement with the loopback cable (output L -> input L): one impulse per second, no tones
// TEST_MODE 3: GUITAR -> NAM (ADELnam engine, quality 1.0, up to 3 lanes in parallel) -> output L.
//   The models (SD:/model0.adn, model1.adn, model2.adn, converted with nam2adn.py) are loaded at startup and updated over Wi-Fi.
//   Calibration as ADELnam on Linux: Calibration (dBu of 0 dBFS of the input, Pisound Gain at 0) and "Normalized" output.
//   With no models loaded it is a passthrough. The loopback cable output->input must NOT be connected.
// TEST_MODE 2: PASSTHROUGH input L -> output L (guitar -> Pisound -> amp), unity gain, output R muted.
//   IMPORTANT: in this mode the loopback cable output->input must NOT be connected (feedback).
#define TEST_MODE		3
#define NAM_CAL_DBU		1.5f		// dBu corresponding to 0 dBFS at the input with the Pisound Gain at 0 (measured 2026-10-10): default value, changed from the page (Input panel)
#define MIDI_PC_DEBOUNCE_MS	0		// MIDI Program Change: the last one is applied after this many ms without other messages (it was 100: now the change is fast and PCs in quick succession are queued)
#define SEAMLESS_PRESET		1		// preset change without interruption (prepared aside, delay and reverb tails kept); 0 = the usual loading
#define NAM_CAL_OFFSET_DB	6.0f		// default calibration offset: +6 dB is the historical ADELnam/PiPedal sound; 0 = TONE3000 guide / NeuralAudio formula
#ifndef NO_AUDIO_START
#define NO_AUDIO_START		0		// 1 = test: does not start the audio (to isolate memory defects)
#endif
#ifndef NO_AUDIO_PROC
#define NO_AUDIO_PROC		0		// 1 = test: the audio starts but the callbacks process nothing
#endif
#define NAM_DIAG		0		// 1 = every 6 s compare real time / re-read and send the raw input to the log (heavy)
#define NAM_LOOPTEST		0		// 1 = output L: 880 Hz tone at -20 dBFS (L->L loopback cable) and recording of the input; 0 = normal
#define NAM_LANE_MASK		0x7		// lanes processed (bit 0 = lane 0): 0x1 only one, 0x7 all (sound test)
#define NAM_PARALLEL_GAIN	0.7071f		// gain applied when two or three lanes are in parallel (sum / square root of 2)
#define PULSE_LEVEL		0.2f		// amplitude of the impulse (with the Pisound Volume at maximum, 0.2 does not saturate the input)
#define PULSE_DETECT		0.004f		// detection threshold on the input (fraction of full scale)

// Pisound GPIOs (pisound-pi5 overlay of kernel 6.18.50, BCM chip numbers)
#define PIN_OSR0		13
#define PIN_OSR1		26
#define PIN_OSR2		16
#define PIN_RESET_AUDIO		12
#define PIN_RESET_MCU		24
#define PIN_BUTTON		17		// Pisound button (from the Linux overlay: GPIO 17, active low, with pull-up)
#define BUTTON_HOLD_TICKS	30		// held down for 3 s (30 cycles of 100 ms): concert mode (no network) and return to normal mode

// Addresses of your own PC / build machine. They are NOT stored here: copy config_local.h.example to config_local.h
// (ignored by git) and put your real addresses in it; the placeholders below are only used when that file is missing.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

// syslog server (unprivileged UDP port)
#ifndef SYSLOG_SERVER_IP
#define SYSLOG_SERVER_IP	{192, 168, 1, 10}
#endif
#define LOG_SERVER_PORT       5514

// over-the-air updates (OTA): only this address may write update.img / update.sig
#ifndef OTA_ALLOWED_IP
#define OTA_ALLOWED_IP		{192, 168, 1, 10}
#endif

#endif

// MIDI hardware (USB controller and the Pisound's DIN socket): off in the normal image. It is turned on by compiling with -DMIDI_HW ("midi" image, to be tried with you present):
// loading update-usbdin3 without this protection rebooted the board twice (UpdatePlugAndPlay with USB initialized without detection: assertion failed m_bPlugAndPlay)
#ifndef MIDI_HW
#define MIDI_NO_USB
#define MIDI_NO_DIN
#endif
