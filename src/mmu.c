#include "mmu.h"
#include "emulator.h"
#include "gamepad.h"
#include "utils.h"
// #include <cstdint>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_mem(Emulator *emulator) {
  Memory *mem = &emulator->mem;
  mem->emulator = emulator;
  mem->mapper = &emulator->mapper;

  memset(mem->RAM, 0, RAM_SIZE);
  init_joypad(&mem->joy1, 0);
  init_joypad(&mem->joy2, 1);
}

uint8_t *get_ptr(Memory *mem, uint16_t address) {
  if (address < 0x2000)
    return mem->RAM + (address % 0x800);
  if (address > 0x6000 && address < 0x8000 && mem->mapper->PRG_RAM != NULL)
    return mem->mapper->PRG_RAM + (address - 0x6000);
  return NULL;
}

static int gameState = 0;
static int runningTimer = 0;
static enum {
  MDIR_RIGHT = 1,
  MDIR_LEFT = 2,
} direction;
static enum {
  WALKING = 0,
  RUNNING,
} runState;

static int8_t correct_speed = 1;
static int max_left = 1;
static int max_right = 1;

// smb1 specific stuff
bool smb1_read_intercepts(Memory *mem, uint16_t address, uint8_t *result) {
  int16_t analog_value = get_analog_value();
  if (abs(analog_value) >= (INT16_MAX / 4)) {
    runState = RUNNING;
  } else {
    runState = WALKING;
  }
  if (address == 0x0057) {
    if (gameState == 8) {
      int max = 1;
      if (max_right != 0 && max_left != 0) {
        if (abs(max_left) == abs(max_right)) {
          max = max_right;
        } else if (abs(max_left) >= abs(max_right)) {
          max = max_left;
        } else {
          max = max_right;
        }
      }
      int speed = analog_value / ((INT16_MAX - JOYSTICK_DEADZONE) / max);
      // printf("%d, %d\n", abs(max_left), abs(max_right));
      if (correct_speed <= 0) {
        // speed = -speed;
      }
      if (abs(correct_speed) <= 2) {
        speed = correct_speed;
      }
      *result = speed;

      return true;
    }
  }

  if (address == 0x783) {
    // int16_t speed = (int16_t)((double)analog_value /
    // (double)((INT16_MAX - JOYSTICK_DEADZONE))) *
    // 20.0;
    *result = 10;
    return true;
  }

  return false;
}
// smb1 specific stuff
bool smb1_write_intercepts(Memory *mem, uint16_t address, uint8_t value) {
  if (address == 0x0003) {
    direction = value;
  }
  if (address == 0x000e) {
    gameState = value;
  }
  if (address == 0x783) {
    runningTimer = value;
  }
  if (address == 0x0057) {
    correct_speed = (int8_t)value;
  }
  if (address == 0x0450) {
    max_left = (int8_t)value;
    // printf("max_left -> %d\n", value);
  }
  if (address == 0x0456) {
    max_right = (int8_t)value;
    // printf("max_right -> %d\n", value);
  }
  return false;
}

void write_mem(Memory *mem, uint16_t address, uint8_t value) {
  uint8_t old = mem->bus;
  mem->bus = value;

  if (smb1_write_intercepts(mem, address, value)) {
    return;
  }

  if (address < RAM_END) {
    mem->RAM[address % RAM_SIZE] = value;
    return;
  }

  // resolve mirrored registers
  if (address < IO_REG_MIRRORED_END)
    address = 0x2000 + (address - 0x2000) % 0x8;

  // handle all IO registers
  if (address < IO_REG_END) {
    PPU *ppu = &mem->emulator->ppu;
    APU *apu = &mem->emulator->apu;

    switch (address) {
    case PPU_CTRL:
      ppu->bus = value;
      set_ctrl(ppu, value);
      break;
    case PPU_MASK:
      ppu->bus = value;
      ppu->mask = value;
      break;
    case PPU_SCROLL:
      ppu->bus = value;
      set_scroll(ppu, value);
      break;
    case PPU_ADDR:
      ppu->bus = value;
      set_address(ppu, value);
      break;
    case PPU_DATA:
      ppu->bus = value;
      write_ppu(ppu, value);
      break;
    case OAM_ADDR:
      ppu->bus = value;
      set_oam_address(ppu, value);
      break;
    case OAM_DMA:
      dma(ppu, value);
      break;
    case OAM_DATA:
      ppu->bus = value;
      write_oam(ppu, value);
      break;
    case PPU_STATUS:
      ppu->bus = value;
      break;
    case JOY1:
      write_joypad(&mem->joy1, value);
      write_joypad(&mem->joy2, value);
      mem->bus = (old & 0xf0) | value & 0xf;
      break;
    case APU_P1_CTRL:
      set_pulse_ctrl(&apu->pulse1, value);
      break;
    case APU_P2_CTRL:
      set_pulse_ctrl(&apu->pulse2, value);
      break;
    case APU_P1_RAMP:
      set_pulse_sweep(&apu->pulse1, value);
      break;
    case APU_P2_RAMP:
      set_pulse_sweep(&apu->pulse2, value);
      break;
    case APU_P1_FT:
      set_pulse_timer(&apu->pulse1, value);
      break;
    case APU_P2_FT:
      set_pulse_timer(&apu->pulse2, value);
      break;
    case APU_P1_CT:
      set_pulse_length_counter(&apu->pulse1, value);
      break;
    case APU_P2_CT:
      set_pulse_length_counter(&apu->pulse2, value);
      break;
    case APU_TRI_LINEAR_COUNTER:
      set_tri_counter(&apu->triangle, value);
      break;
    case APU_TRI_FREQ1:
      set_tri_timer_low(&apu->triangle, value);
      break;
    case APU_TRI_FREQ2:
      set_tri_length(&apu->triangle, value);
      break;
    case APU_NOISE_CTRL:
      set_noise_ctrl(&apu->noise, value);
      break;
    case APU_NOISE_FREQ1:
      set_noise_period(apu, value);
      break;
    case APU_NOISE_FREQ2:
      set_noise_length(&apu->noise, value);
      break;
    case APU_DMC_ADDR:
      set_dmc_addr(&apu->dmc, value);
      break;
    case APU_DMC_CTRL:
      set_dmc_ctrl(apu, value);
      break;
    case APU_DMC_DA:
      set_dmc_da(&apu->dmc, value);
      break;
    case APU_DMC_LEN:
      set_dmc_length(&apu->dmc, value);
      break;
    case FRAME_COUNTER:
      set_frame_counter_ctrl(apu, value);
      break;
    case APU_STATUS:
      set_status(apu, value);
      break;
    default:
      break;
    }
    return;
  }

  mem->mapper->write_ROM(mem->mapper, address, value);
}

uint8_t read_mem(Memory *mem, uint16_t address) {

  uint8_t result = 0;
  if (smb1_read_intercepts(mem, address, &result)) {
    return result;
  }

  if (address < RAM_END) {
    mem->bus = mem->RAM[address % RAM_SIZE];
    return mem->bus;
  }

  // resolve mirrored registers
  if (address < IO_REG_MIRRORED_END) {
    address = 0x2000 + (address - 0x2000) % 0x8;
  }

  // handle all IO registers
  if (address < IO_REG_END) {
    PPU *ppu = &mem->emulator->ppu;
    switch (address) {
    case PPU_STATUS:
      ppu->bus &= 0x1f;
      ppu->bus |= read_status(ppu) & 0xe0;
      mem->bus = ppu->bus;
      return mem->bus;
    case OAM_DATA:
      mem->bus = ppu->bus = read_oam(ppu);
      return mem->bus;
    case PPU_DATA:
      mem->bus = ppu->bus = read_ppu(ppu);
      return mem->bus;
    case PPU_CTRL:
    case PPU_MASK:
    case PPU_SCROLL:
    case PPU_ADDR:
    case OAM_ADDR:
      // ppu open bus
      mem->bus = ppu->bus;
      return mem->bus;
    case JOY1:
      mem->bus &= 0xe0;
      mem->bus |= read_joypad(&mem->joy1) & 0x1f;
      return mem->bus;
    case JOY2:
      mem->bus &= 0xe0;
      mem->bus |= read_joypad(&mem->joy2) & 0x1f;
      return mem->bus;
    case APU_STATUS:
      mem->bus = read_apu_status(&mem->emulator->apu);
      return mem->bus;
    default:
      // open bus
      return mem->bus;
    }
  }

  mem->bus = mem->mapper->read_ROM(mem->mapper, address);
  return mem->bus;
}
