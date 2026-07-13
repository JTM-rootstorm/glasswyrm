#define _GNU_SOURCE
#include <stddef.h>
#include <sys/syscall.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

static const time_t gw_m9_fixed_epoch = (time_t)1767323045;

time_t time(time_t *result) {
  if (result != NULL) *result = gw_m9_fixed_epoch;
  return gw_m9_fixed_epoch;
}

int gettimeofday(struct timeval *value, void *timezone) {
  (void)timezone;
  value->tv_sec = gw_m9_fixed_epoch;
  value->tv_usec = 0;
  return 0;
}

int clock_gettime(clockid_t clock, struct timespec *value) {
  if (clock == CLOCK_REALTIME) {
    value->tv_sec = gw_m9_fixed_epoch;
    value->tv_nsec = 0;
    return 0;
  }
  return (int)syscall(SYS_clock_gettime, clock, value);
}
