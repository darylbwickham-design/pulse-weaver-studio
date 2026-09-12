#include "../../engine/obs-studio/libobs/util/pulse-output-time.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void check(int64_t actual, int64_t expected)
{
	if (actual != expected) throw std::runtime_error("Incorrect output uptime");
}
int main()
{
	// Equal dual tracks must show ten minutes, not twenty.
	const int64_t dual[] = {600000000, 600000000};
	check(pulse_longest_track_time_usec(dual, 2), 600000000);
	// Different starts/frame rates are already represented in microseconds.
	const int64_t staggered[] = {600000000, 570000000};
	check(pulse_longest_track_time_usec(staggered, 2), 600000000);
	const int64_t reversed[] = {570000000, 600000000};
	check(pulse_longest_track_time_usec(reversed, 2), 600000000);
	// Enhanced Broadcasting quality variants must not multiply the clock.
	const int64_t variants[] = {600000000, 599966667, 599933333, 600000000, 599933333};
	check(pulse_longest_track_time_usec(variants, 5), 600000000);
	const int64_t starting[] = {std::numeric_limits<int64_t>::min(), -33333, 0};
	check(pulse_longest_track_time_usec(starting, 3), 0);
	check(pulse_longest_track_time_usec(nullptr, 0), 0);
	const int64_t longStream[] = {90000000000LL, 89999999999LL};
	check(pulse_longest_track_time_usec(longStream, 2), 90000000000LL);
	std::cout << "PASS: dual, staggered, reversed, five-track, empty/startup and 25-hour uptime\n";
}
