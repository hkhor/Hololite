/**************************************************************************
 * Copyright (c) 2012, Intel Corporation.
 * All Rights Reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Javier Torres Castillo <javier.torres.castillo@intel.com>
 */
#include "df_rgx_defs.h"
#include "dev_freq_debug.h"
#include "dev_freq_graphics_pm.h"

extern int is_tng_a0;
extern int gpu_freq_get_max_fuse_setting(void);

struct gpu_freq_thresholds a_governor_profile[] = {
			/* low, high thresholds for SimpleOndemand profile */
			{65, 87},
			/* low, high thresholds for Power Save profile*/
			{78, 92},
			/* low, high Performance thresholds */
			{38, 48},
			/* low, high Custom (SimpleOndemand Old) */
			{67, 85}
			};

struct gpu_freq_thresholds simple_ondemand_loads[] = {
	{20, 20}, /* 200 Mhz */
	{33, 33}, /* 213 Mhz */
	{46, 46}, /* 266 Mhz */
	{58, 58}, /* 320 Mhz */
	{66, 66}, /* 355 Mhz */
	{78, 78}, /* 400 Mhz */
	{87, 87}, /* 457 Mhz */
	{92, 92}  /* 533 Mhz */
};

/**
 * df_rgx_is_valid_freq() - Determines if We are about to use
 * a valid frequency.
 * @freq: frequency to be validated.
 *
 * Function return value: 1 if Valid 0 if not.
 */
unsigned int df_rgx_is_valid_freq(unsigned long int freq)
{
	unsigned int valid = 0;
	int i;
	int a_size = sku_levels();

	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s freq: %lu\n",
			__func__, freq);

	for (i = 0; i < a_size; i++) {
		if (freq == a_available_state_freq[i].freq) {
			valid = 1;
			break;
		}
	}

	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s valid: %u\n",
			__func__, valid);

	return valid;
}

/**
 * df_rgx_get_util_record_index_by_freq() - Obtains the index to a
 * a record from the avalable frequencies table.
 * @freq: frequency to be validated.
 *
 * Function return value: the index if found, -1 if not.
 */
int df_rgx_get_util_record_index_by_freq(unsigned long freq)
{
	int i = 0;
	int n_levels = sku_levels();

	for (i = 0; i < n_levels; i++) {
		if (freq == a_available_state_freq[i].freq)
			break;
	}

	if (i == n_levels)
		i = -1;

	return i;
}

/**
 * df_rgx_request_burst() - Decides if dfrgx needs to BURST, UNBURST
 * or keep the current frequency level.
 * @pdfrgx_data: Dynamic turbo information
 * @util_percentage: percentage of utilization in active state.
 * Function return value: DFRGX_NO_BURST_REQ, DFRGX_BURST_REQ,
 * DFRGX_UNBURST_REQ.
 */
unsigned int df_rgx_request_burst(struct df_rgx_data_s *pdfrgx_data,
			int util_percentage)
{
	int current_index = pdfrgx_data->gpu_utilization_record_index;
	unsigned long freq = a_available_state_freq[current_index].freq;
	int new_index;
	unsigned int burst = DFRGX_NO_BURST_REQ;
	unsigned int th_high_util;
	unsigned int th_low_util;

	new_index = df_rgx_get_util_record_index_by_freq(freq);

	th_high_util = a_governor_profile[pdfrgx_data->g_profile_index].util_th_high;
	th_low_util = a_governor_profile[pdfrgx_data->g_profile_index].util_th_low;

	if (unlikely(new_index < 0))
		goto out;

	/* Decide unburst/burst based on utilization*/
	if (util_percentage > th_high_util && new_index < pdfrgx_data->g_max_freq_index) {
		/* Provide recommended burst*/
		pdfrgx_data->gpu_utilization_record_index += 1;
		burst = DFRGX_BURST_REQ;
	} else if (util_percentage < th_low_util && new_index > pdfrgx_data->g_min_freq_index) {
		/* Provide recommended unburst*/
		pdfrgx_data->gpu_utilization_record_index -= 1;
		burst = DFRGX_UNBURST_REQ;
	} else if (new_index < pdfrgx_data->g_min_freq_index) {
		/* If frequency is throttled, request return to min */
		pdfrgx_data->gpu_utilization_record_index = pdfrgx_data->g_min_freq_index;
		burst = DFRGX_UNBURST_REQ;
	}

out:
	return burst;
}

/**
 * df_rgx_ondemand_burst() - Decides if dfrgx needs to BURST, UNBURST
 * or keep the current frequency level with simple_ondemand gov.
 * @pdfrgx_data: Dynamic turbo information
 * @util_percentage: percentage of utilization in active state.
 * Function return value: DFRGX_NO_BURST_REQ, DFRGX_BURST_REQ,
 * DFRGX_UNBURST_REQ.
 */
unsigned int df_rgx_ondemand_burst(struct df_rgx_data_s *pdfrgx_data,
			int util_percentage)
{
	unsigned int burst = DFRGX_NO_BURST_REQ;
	int i;
	int target_index = -1; /* Check For Modification (Impossible Value) */
	int current_index;

	/* Check If Current Freq Is Valid */
	current_index =
	df_rgx_get_util_record_index_by_freq(a_available_state_freq[pdfrgx_data->gpu_utilization_record_index].freq);

	if (unlikely(current_index < 0))
		goto out;

	/* Check For Change Requirement */
	if (unlikely(util_percentage >= simple_ondemand_loads[current_index].util_th_high
	&& (current_index + 1) <= pdfrgx_data->g_max_freq_index
	&& util_percentage < simple_ondemand_loads[current_index + 1].util_th_high)
	||
	unlikely(current_index == pdfrgx_data->g_max_freq_index
	&& util_percentage >= simple_ondemand_loads[current_index].util_th_high))
		goto out;

	/* Throttling Code */
	if (current_index < pdfrgx_data->g_min_freq_index
	|| util_percentage < simple_ondemand_loads[pdfrgx_data->g_min_freq_index].util_th_high
	|| !df_rgx_is_active()) {
		pdfrgx_data->gpu_utilization_record_index = pdfrgx_data->g_min_freq_index;
		burst = DFRGX_UNBURST_REQ;
		goto out;
	}

	for (i = pdfrgx_data->g_max_freq_index; i >= pdfrgx_data->g_min_freq_index; i--) {
		if (unlikely(util_percentage >= simple_ondemand_loads[i].util_th_high)) {
			target_index = i;
			burst = DFRGX_BURST_REQ;
			break;
		}
	}

	if (unlikely(target_index == -1))
		goto out;

	pdfrgx_data->gpu_utilization_record_index = target_index;

out:
	return burst;
}

/**
 * df_rgx_set_governor_profile() -Updates the thresholds based on the governor.
 * @governor_name: governor id
 * @g_dfrgx_data: Dynamic turbo information
 * Function return value: 1 if changed, 0 otherwise.
 */
int df_rgx_set_governor_profile(const char *governor_name,
			struct df_rgx_data_s *g_dfrgx)
{
	int ret = 0;

	if (!strncmp(governor_name, "performance", DEVFREQ_NAME_LEN))
		g_dfrgx->g_profile_index = DFRGX_TURBO_PROFILE_PERFORMANCE;
	else if (!strncmp(governor_name, "powersave", DEVFREQ_NAME_LEN))
		g_dfrgx->g_profile_index = DFRGX_TURBO_PROFILE_POWERSAVE;
	else if (!strncmp(governor_name, "simple_ondemand", DEVFREQ_NAME_LEN)) {
		g_dfrgx->g_profile_index = DFRGX_TURBO_PROFILE_SIMPLE_ON_DEMAND;
		ret = 1;
	} else if (!strncmp(governor_name, "userspace", DEVFREQ_NAME_LEN))
		g_dfrgx->g_profile_index = DFRGX_TURBO_PROFILE_USERSPACE;

	return ret;
}

/**
 * df_rgx_is_max_fuse_set() -Checks setting of fuse to determine if
 * additional max frequency (640 MHz) should be supported.
 * Function return value: 1 if set, 0 otherwise.
 */
int df_rgx_is_max_fuse_set(void)
{
	int fuse_val = 0;

	fuse_val = gpu_freq_get_max_fuse_setting();

	if (fuse_val == 0x4) /* 640 MHz */
		return 1;

	return 0;
}
