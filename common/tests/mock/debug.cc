#include "debug.h"
#include <assert.h>
#include <stdlib.h>
#include "CppUTestExt/MockSupport.h"

uint8_t Debug::channel_read_buffer[Channel::kDataAreaSizeMax];
uint8_t Debug::channel_write_buffer[Channel::kDataAreaSizeMax];
void Debug::InitChannelBuffer() {
  for(int i = 0; i < Channel::kDataAreaSizeMax; i++) {
    channel_read_buffer[i] = rand();
    channel_write_buffer[i] = rand();
  }
}

void assert_func() {
  mock().actualCall("assert");
}

void panic_func(const char *str) {
  mock().actualCall("panic");
}
