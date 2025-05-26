#pragma once

#include <SDL.h>

#define MAX_PADS 2
#define CONTROLLER_KEY_COUNT 11
#define JOYSTICK_DEADZONE 10000

typedef SDL_Gamepad GamePad;
struct JoyPad;

void init_pads();
void gamepad_mapper(struct JoyPad *joyPad, SDL_Event *event);

int16_t get_analog_value();