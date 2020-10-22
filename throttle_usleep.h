#pragma once

#ifdef NO_THROTTLING
#define throttle_usleep(x) ((void)0)
#else
#include <unistd.h>
#define throttle_usleep usleep
#endif
