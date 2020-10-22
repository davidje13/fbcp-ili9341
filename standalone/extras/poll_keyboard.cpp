#include <linux/input.h> // input_event
#include <fcntl.h> // open, O_RDONLY, O_NONBLOCK
#include <unistd.h> // read, close
#include <stdio.h> // printf
#include <stdint.h> // uint64_t

#include "../config.h"
#include "poll_keyboard.h"
#include "../../library/include/tick.h"

#if defined(BACKLIGHT_CONTROL_FROM_KEYBOARD) && defined(TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY)
#define READ_KEYBOARD_ENABLED
#endif

#ifdef READ_KEYBOARD_ENABLED

static int key_fd = -1;

void InitPollKeyboardSystem()
{
  key_fd = open(KEYBOARD_INPUT_FILE, O_RDONLY|O_NONBLOCK);
  if (key_fd < 0) printf("Warning: cannot open keyboard input file " KEYBOARD_INPUT_FILE "! Try double checking that it exists, or reconfigure it in keyboard.cpp, or remove line '#define BACKLIGHT_CONTROL_FROM_KEYBOARD' in config.h if you do not want keyboard activity to factor into backlight control.\n");
}

static int ReadKeyboard()
{
  if (key_fd < 0) return 0;
  struct input_event ev;
  ssize_t bytesRead = -1;
  int numRead = 0;
  do
  {
    bytesRead = read(key_fd, &ev, sizeof(struct input_event));
    if (bytesRead >= sizeof(struct input_event))
    {
      if (ev.type == 1 && ev.code != 0) // key up or down
      {
//        printf("time: %d %d type: %d, code: %d, value: %d\n", ev.time.tv_sec, ev.time.tv_usec, ev.type, ev.code, ev.value);
        ++numRead;
      }
    }
  } while(bytesRead > 0);
  return numRead;
}

void DeinitPollKeyboardSystem()
{
  if (key_fd >= 0)
  {
    close(key_fd);
    key_fd = -1;
  }
}

static uint64_t lastKeyboardPressTime = 0;
static uint64_t lastKeyboardPressCheckTime = 0;

uint64_t TimeSinceLastKeyboardPress(void)
{
  uint64_t now = tick();
  if (now - lastKeyboardPressCheckTime >= 250000) // ReadKeyboard() takes about 8 usecs on Pi 3B, so 250msecs poll interval should be fine
  {
    lastKeyboardPressCheckTime = now;
    if (ReadKeyboard())
      lastKeyboardPressTime = now;
  }
  return now - lastKeyboardPressTime;
}

#else

void InitPollKeyboardSystem() {}
void DeinitPollKeyboardSystem() {}
uint64_t TimeSinceLastKeyboardPress(void) { return 0; }

#endif
