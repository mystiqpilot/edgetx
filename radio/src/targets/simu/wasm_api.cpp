/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

// WebAssembly API of the EdgeTX 2.11 simulator.
//
// EdgeTX 3.0 builds its simulator as a WASM module whose C API is
// targets/simu/simulib.h. 2.11 has no simulib: this file exports the same
// names (and imports the same host functions) over 2.11's own simulator
// (simpgmspace.cpp, the code Companion's simulator plugin drives), so one host
// can run 2.11 and 3.0 modules. It is compiled only for a WASI build, see the
// WASI section of targets/simu/CMakeLists.txt and tools/build-wasm-judge.sh.
//
// Differences from 3.0's simulib.h:
// - simuStart(tests, utcOffset): utcOffset is ignored, the radio clock is UTC.
// - simuLcdNotify() is never called: poll simuLcdChanged().
// - Not exported: audio (simuQueueAudio is not imported), telemetry, trainer,
//   aux serial, Lua reload, haptic, custom switch LEDs.
// - simuRotaryEncoderEvent() handles ROTARY_ENCODER_NAVIGATION radios only;
//   on the others the host presses the keys itself (Companion did the same).
// - EdgeTX 2.11's simulator has no 10 ms timer task: the host calls
//   simuPer10ms() every 10 ms while the radio runs (Companion did this with a
//   QTimer). Without it timers, trims and storage writes stand still.
// - settingsPath is not an overlay in 2.11: RADIO/ and the MODELS/*.yml files
//   are read from and written to settingsPath only (simufatfs.cpp). Give both
//   paths the same directory to run a card as it is.
// Extra: simuJudgeMarkDirty(), see below.

#include "edgetx.h"
#include "simpgmspace.h"
#include "simulcd.h"
#include "switches.h"
#include "input_mapping.h"
#include "hal/adc_driver.h"
#include "hal/rotary_encoder.h"
#include "rtos.h"

#include <string>

#define WASM_EXPORT_AS(name) extern "C" __attribute__((export_name(#name)))
#define WASM_IMPORT_AS(name) \
  extern "C" __attribute__((import_module("env"), import_name(#name)))

// -- WASM imports (provided by the host) --

// ADC value of input idx, 0..4096, centre 2048 (as 3.0's simulib.h)
WASM_IMPORT_AS(simuGetAnalog) uint16_t hostGetAnalog(uint8_t idx);
// debug/trace output text
WASM_IMPORT_AS(simuTrace) void hostTrace(const char* text);

// adc_driver.cpp reads the analog inputs through this hook (Companion's
// plugin and the SDL simulator define their own)
uint16_t simu_get_analog(uint8_t idx) { return hostGetAnalog(idx); }

// -- Lifecycle --

static std::string sdPath, settingsPath;

WASM_EXPORT_AS(simuInit) void wasmSimuInit()
{
  traceCallback = hostTrace;
  simuInit();
}

// Set the SD card and settings paths before simuStart() (2.11: see above).
WASM_EXPORT_AS(simuFatfsSetPaths)
void wasmSimuFatfsSetPaths(const char* sd, const char* settings)
{
  sdPath = sd ? sd : "";
  settingsPath = settings ? settings : "";
}

// Empty card: create radio.yml and the first model without the STORAGE
// WARNING alert. Call before simuStart().
WASM_EXPORT_AS(simuCreateDefaults) void wasmSimuCreateDefaults()
{
  simuCreateDefaults();
}

// tests = false: no splash, calibration or start-up checks
WASM_EXPORT_AS(simuStart) void wasmSimuStart(int32_t tests, int32_t utcOffset)
{
  (void)utcOffset;
  simuStart(tests != 0, sdPath.c_str(), settingsPath.c_str());
}

// Power off: edgeTxClose() writes the current model and radio.yml
WASM_EXPORT_AS(simuStop) void wasmSimuStop() { simuStop(); }
WASM_EXPORT_AS(simuIsRunning) int32_t wasmSimuIsRunning()
{
  return simuIsRunning();
}

// Not in 3.0's simulib: mark the current model and the radio settings as
// changed, so that the next storage check (or simuStop()) writes both in this
// firmware's own format. Setting a trim to its own value does the same, except
// for a model whose trims are all disabled.
WASM_EXPORT_AS(simuJudgeMarkDirty) void wasmSimuJudgeMarkDirty()
{
  storageDirty(EE_MODEL | EE_GENERAL);
}

// -- Input --

WASM_EXPORT_AS(simuSetKey) void wasmSimuSetKey(int32_t key, int32_t state)
{
  simuSetKey(key, state != 0);
}
WASM_EXPORT_AS(simuSetTrim) void wasmSimuSetTrim(int32_t trim, int32_t state)
{
  simuSetTrim(trim, state != 0);
}
WASM_EXPORT_AS(simuSetSwitch) void wasmSimuSetSwitch(int32_t sw, int32_t state)
{
  simuSetSwitch(sw, state);
}

// The host drives the 10 ms tick (see above)
void per10ms();
WASM_EXPORT_AS(simuPer10ms) void wasmSimuPer10ms() { per10ms(); }

// Touch: simuTouchDown(x, y) on press and again on every move while pressed,
// simuTouchUp() on release. 2.11's simulator has no touchPanelDown/Up(): this
// is Companion's OpenTxSimulator::touchEvent() (tap count, slides).
#if defined(HARDWARE_TOUCH)
#define SIMU_TAP_TIME 25  // 10 ms ticks, as simu.cpp
static bool touchPressed = false;
static tmr10ms_t touchDownTime = 0, touchTapTime = 0;
static short touchTapCount = 0;
#endif

WASM_EXPORT_AS(simuTouchDown) void wasmSimuTouchDown(int32_t x, int32_t y)
{
#if defined(HARDWARE_TOUCH)
  tmr10ms_t now = get_tmr10ms();
  simTouchState.tapCount = 0;
  if (!touchPressed) {
    touchPressed = true;
    simTouchState.event = TE_DOWN;
    simTouchState.startX = simTouchState.x = x;
    simTouchState.startY = simTouchState.y = y;
    touchDownTime = now;
  } else {
    simTouchState.deltaX += x - simTouchState.x;
    simTouchState.deltaY += y - simTouchState.y;
    if (simTouchState.event == TE_SLIDE || abs(simTouchState.deltaX) >= SLIDE_RANGE ||
        abs(simTouchState.deltaY) >= SLIDE_RANGE) {
      simTouchState.event = TE_SLIDE;
      simTouchState.x = x;
      simTouchState.y = y;
    }
  }
  simTouchOccured = true;
#else
  (void)x;
  (void)y;
#endif
}
WASM_EXPORT_AS(simuTouchUp) void wasmSimuTouchUp()
{
#if defined(HARDWARE_TOUCH)
  tmr10ms_t now = get_tmr10ms();
  simTouchState.tapCount = 0;
  touchPressed = false;
  if (simTouchState.event == TE_DOWN) {
    simTouchState.event = TE_UP;
    simTouchState.x = simTouchState.startX;
    simTouchState.y = simTouchState.startY;
    if (now - touchDownTime <= SIMU_TAP_TIME) {
      if (now - touchTapTime > SIMU_TAP_TIME)
        touchTapCount = 1;
      else
        touchTapCount++;
      simTouchState.tapCount = touchTapCount;
      touchTapTime = now;
    }
  } else {
    simTouchState.event = TE_SLIDE_END;
  }
  simTouchOccured = true;
#endif
}

extern volatile rotenc_t rotencValue;
extern volatile uint32_t rotencDt;

// positive steps = clockwise (Companion's OpenTxSimulator::rotaryEncoderEvent)
WASM_EXPORT_AS(simuRotaryEncoderEvent)
void wasmSimuRotaryEncoderEvent(int32_t steps)
{
#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
  static uint32_t lastTick = 0;
  if (steps != 0) {
    if (g_eeGeneral.rotEncMode == ROTARY_ENCODER_MODE_INVERT_BOTH) steps *= -1;
    rotencValue += steps * ROTARY_ENCODER_GRANULARITY;
    uint32_t now = RTOS_GET_MS();
    rotencDt += now - lastTick;
    lastTick = now;
  }
#else
  (void)steps;
#endif
}

// 0=LUA, 1=ROTARY_ENC, 2=ROTARY_ENC_NAV, 3=TELEM_FRSKY_SPORT, 4=SERIAL_AUX1,
// 5=SERIAL_AUX2 (3.0's simulib.h); telemetry and aux serial are not bridged
WASM_EXPORT_AS(simuGetCapability) int32_t wasmSimuGetCapability(int32_t cap)
{
  switch (cap) {
    case 0:
#if defined(LUA)
      return 1;
#else
      return 0;
#endif
    case 2:
#if defined(ROTARY_ENCODER_NAVIGATION)
      return 1;
#else
      return 0;
#endif
    default:
      return 0;
  }
}

// -- LCD (depth in bits per pixel: 1, 4 or 16) --

WASM_EXPORT_AS(simuLcdGetWidth) uint32_t wasmSimuLcdGetWidth() { return LCD_W; }
WASM_EXPORT_AS(simuLcdGetHeight) uint32_t wasmSimuLcdGetHeight()
{
  return LCD_H;
}
WASM_EXPORT_AS(simuLcdGetDepth) uint32_t wasmSimuLcdGetDepth()
{
#if defined(COLORLCD)
  return 16;
#elif LCD_W == 212
  return 4;
#else
  return 1;
#endif
}
WASM_EXPORT_AS(simuLcdChanged) int32_t wasmSimuLcdChanged()
{
  bool changed = simuLcdRefresh;
  simuLcdRefresh = false;
  return changed;
}
WASM_EXPORT_AS(simuLcdCopy)
uint32_t wasmSimuLcdCopy(uint8_t* buf, uint32_t maxLen)
{
  uint32_t size = DISPLAY_BUFFER_SIZE * sizeof(pixel_t);
  if (size > maxLen) size = maxLen;
  if (simuLcdBuf) memcpy(buf, simuLcdBuf, size);
  return size;
}
// the host has consumed the frame (colour radios wait for it)
WASM_EXPORT_AS(simuLcdFlushed) void wasmSimuLcdFlushed()
{
#if defined(COLORLCD)
  if (simuIsRunning()) lcdFlushed();
#endif
}
WASM_EXPORT_AS(simuGetBacklightState) int32_t wasmSimuGetBacklightState()
{
  return isBacklightEnabled();
}

// -- Trims (idx mapped through inputMappingConvertMode, current flight mode) --

WASM_EXPORT_AS(simuGetTrimValue) int32_t wasmSimuGetTrimValue(int32_t idx)
{
  unsigned i = inputMappingConvertMode(idx);
  return getTrimValue(getTrimFlightMode(getFlightMode(), i), i);
}
WASM_EXPORT_AS(simuSetTrimValue)
void wasmSimuSetTrimValue(int32_t idx, int32_t value)
{
  unsigned i = inputMappingConvertMode(idx);
  setTrimValue(getTrimFlightMode(getFlightMode(), i), i, value);
}
WASM_EXPORT_AS(simuGetTrimRange) int32_t wasmSimuGetTrimRange()
{
  return g_model.extendedTrims ? TRIM_EXTENDED_MAX : TRIM_MAX;
}

// -- Outputs, logical switches, flight modes, global variables --

WASM_EXPORT_AS(simuGetNumChannels) uint32_t wasmSimuGetNumChannels()
{
  return MAX_OUTPUT_CHANNELS;
}
WASM_EXPORT_AS(simuCopyChannelOutputs)
uint32_t wasmSimuCopyChannelOutputs(int16_t* buf, uint32_t maxCount)
{
  uint32_t n = MAX_OUTPUT_CHANNELS < maxCount ? MAX_OUTPUT_CHANNELS : maxCount;
  memcpy(buf, channelOutputs, n * sizeof(int16_t));
  return n;
}
WASM_EXPORT_AS(simuCopyMixOutputs)
uint32_t wasmSimuCopyMixOutputs(int16_t* buf, uint32_t maxCount)
{
  uint32_t n = MAX_OUTPUT_CHANNELS < maxCount ? MAX_OUTPUT_CHANNELS : maxCount;
  memcpy(buf, ex_chans, n * sizeof(int16_t));
  return n;
}
WASM_EXPORT_AS(simuGetNumLogicalSwitches)
uint32_t wasmSimuGetNumLogicalSwitches()
{
  return MAX_LOGICAL_SWITCHES;
}
WASM_EXPORT_AS(simuCopyLogicalSwitches)
uint32_t wasmSimuCopyLogicalSwitches(uint8_t* buf, uint32_t maxCount)
{
  uint32_t n = MAX_LOGICAL_SWITCHES < maxCount ? MAX_LOGICAL_SWITCHES : maxCount;
  for (uint32_t i = 0; i < n; i++)
    buf[i] = getSwitch(SWSRC_FIRST_LOGICAL_SWITCH + i, 0) ? 1 : 0;
  return n;
}
WASM_EXPORT_AS(simuGetFlightMode) int32_t wasmSimuGetFlightMode()
{
  return getFlightMode();
}
WASM_EXPORT_AS(simuGetNumFlightModes) uint32_t wasmSimuGetNumFlightModes()
{
  return MAX_FLIGHT_MODES;
}
WASM_EXPORT_AS(simuGetNumGVars) uint32_t wasmSimuGetNumGVars()
{
#if defined(GVARS)
  return MAX_GVARS;
#else
  return 0;
#endif
}
// encoded as 3.0's gVarMode_t: value[15:0] | mode[23:16] | prec[25:24] | unit[27:26]
WASM_EXPORT_AS(simuGetGVar) int32_t wasmSimuGetGVar(int32_t gv, int32_t fm)
{
#if defined(GVARS)
  if (gv >= 0 && gv < MAX_GVARS && fm >= 0 && fm < MAX_FLIGHT_MODES) {
    uint8_t prec = g_model.gvars[gv].prec;
    uint8_t unit = g_model.gvars[gv].unit;
    int16_t value = (int16_t)GVAR_VALUE(gv, getGVarFlightMode(fm, gv));
    return (((unit & 0x3) << 26) | ((prec & 0x3) << 24) | ((fm & 0xFF) << 16) |
            (value & 0xFFFF));
  }
#else
  (void)gv;
  (void)fm;
#endif
  return 0;
}
