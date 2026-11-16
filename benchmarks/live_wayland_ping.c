// SPDX-License-Identifier: GPL-2.0
// Live GNOME/Mutter Wayland Compositor Wire Round-Trip Benchmark
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <wayland-client.h>

static inline uint64_t nsec_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t va = *(const uint64_t *)a;
	uint64_t vb = *(const uint64_t *)b;
	return (va > vb) - (va < vb);
}

int main(int argc, char **argv)
{
	int trials = (argc >= 2) ? atoi(argv[1]) : 10;
	int rts_per_trial = (argc >= 3) ? atoi(argv[2]) : 2000;

	struct wl_display *dpy = wl_display_connect(NULL);
	if (!dpy) {
		fprintf(stderr, "ERROR: Failed to connect to live WAYLAND_DISPLAY\n");
		return 1;
	}

	// Warmup 200 live round-trips against Mutter
	for (int i = 0; i < 200; i++)
		wl_display_roundtrip(dpy);

	uint64_t *lats = malloc(sizeof(uint64_t) * (size_t)rts_per_trial);
	for (int t = 0; t < trials; t++) {
		struct rusage ru0, ru1;
		getrusage(RUSAGE_SELF, &ru0);
		uint64_t sum = 0;
		for (int i = 0; i < rts_per_trial; i++) {
			uint64_t t0 = nsec_now();
			if (wl_display_roundtrip(dpy) < 0) {
				fprintf(stderr, "ERROR: wl_display_roundtrip failed\n");
				wl_display_disconnect(dpy);
				free(lats);
				return 1;
			}
			uint64_t dt = nsec_now() - t0;
			lats[i] = dt;
			sum += dt;
		}
		getrusage(RUSAGE_SELF, &ru1);
		qsort(lats, (size_t)rts_per_trial, sizeof(uint64_t), cmp_u64);

		double mean_us = ((double)sum / rts_per_trial) / 1000.0;
		double p50_us  = (double)lats[rts_per_trial / 2] / 1000.0;
		double p99_us  = (double)lats[(size_t)(rts_per_trial * 0.99)] / 1000.0;
		double sys_ms  = ((double)(ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec) * 1000.0) +
				 ((double)(ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec) / 1000.0);

		printf("{\"suite\":\"S6_Live_Wayland\",\"metric\":\"mutter_wl_roundtrip_mean_us\",\"trial\":%d,\"value\":%.4f,\"unit\":\"us\",\"better\":\"lower\"}\n", t, mean_us);
		printf("{\"suite\":\"S6_Live_Wayland\",\"metric\":\"mutter_wl_roundtrip_p50_us\",\"trial\":%d,\"value\":%.4f,\"unit\":\"us\",\"better\":\"lower\"}\n", t, p50_us);
		printf("{\"suite\":\"S6_Live_Wayland\",\"metric\":\"mutter_wl_roundtrip_p99_us\",\"trial\":%d,\"value\":%.4f,\"unit\":\"us\",\"better\":\"lower\"}\n", t, p99_us);
		printf("{\"suite\":\"S6_Live_Wayland\",\"metric\":\"mutter_wl_roundtrip_sys_ms\",\"trial\":%d,\"value\":%.4f,\"unit\":\"ms\",\"better\":\"lower\"}\n", t, sys_ms);
		fflush(stdout);
	}

	free(lats);
	wl_display_disconnect(dpy);
	return 0;
}
