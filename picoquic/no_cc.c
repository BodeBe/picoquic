/*
* Author: Christian Huitema
* Copyright (c) 2017, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "picoquic_internal.h"
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

#define NB_RTT_RENO 4

/* Many congestion control algorithms run a parallel version of new reno in order
 * to provide a lower bound estimate of either the congestion window or the
 * the minimal bandwidth. This implementation of new reno does not directly
 * refer to the connection and path variables (e.g. cwin) but instead sets
 * its entire state in memory.
 */

typedef struct st_picoquic_no_cc_state_t {
    picoquic_newreno_sim_state_t nrss;
    picoquic_min_max_rtt_t rtt_filter;
} picoquic_no_cc_state_t;

static void picoquic_no_cc_reset(picoquic_no_cc_state_t* nr_state, picoquic_path_t* path_x)
{
    memset(nr_state, 0, sizeof(picoquic_no_cc_state_t));
    nr_state->nrss.cwin = 62500;
    path_x->cwin = 62500;
    picoquic_newreno_sim_reset(&nr_state->nrss);
}

static void picoquic_no_cc_init(picoquic_path_t* path_x, uint64_t current_time)
{
    /* Initialize the state of the congestion control algorithm */
    picoquic_no_cc_state_t* nr_state = (picoquic_no_cc_state_t*)malloc(sizeof(picoquic_no_cc_state_t));
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(current_time);
#endif

    if (nr_state != NULL) {
        picoquic_no_cc_reset(nr_state, path_x);
        path_x->congestion_alg_state = nr_state;
        nr_state->nrss.cwin = 62500;
        path_x->cwin = 62500;
    }
    else {
        path_x->congestion_alg_state = NULL;
        path_x->cwin = 62500;
    }
}

/*
 * Properly implementing New Reno requires managing a number of
 * signals, such as packet losses or acknowledgements. We attempt
 * to condensate all that in a single API, which could be shared
 * by many different congestion control algorithms.
 */
static void picoquic_no_cc_notify(
    picoquic_cnx_t * cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    uint64_t rtt_measurement,
    uint64_t one_way_delay,
    uint64_t nb_bytes_acknowledged,
    uint64_t lost_packet_number,
    uint64_t current_time)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(lost_packet_number);
#endif
    picoquic_no_cc_state_t* nr_state = (picoquic_no_cc_state_t*)path_x->congestion_alg_state;

    path_x->is_cc_data_updated = 1;

    if (nr_state != NULL) {
        switch (notification) {
        case picoquic_congestion_notification_acknowledgement:
            if (path_x->last_time_acked_data_frame_sent > path_x->last_sender_limited_time) {
                picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, nb_bytes_acknowledged, lost_packet_number, current_time);
                path_x->cwin = nr_state->nrss.cwin;
            }
            break;
        case picoquic_congestion_notification_seed_cwin:
        case picoquic_congestion_notification_ecn_ec:
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_timeout:
            picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, nb_bytes_acknowledged, lost_packet_number, current_time);
            path_x->cwin = nr_state->nrss.cwin;
            break;
        case picoquic_congestion_notification_spurious_repeat:
            picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, nb_bytes_acknowledged, lost_packet_number, current_time);
            path_x->cwin = nr_state->nrss.cwin;
            break;
        case picoquic_congestion_notification_rtt_measurement:
            /* Using RTT increases as signal to get out of initial slow start */
            if (nr_state->nrss.alg_state == picoquic_newreno_alg_slow_start &&
                nr_state->nrss.ssthresh == UINT64_MAX){

                if (path_x->rtt_min > PICOQUIC_TARGET_RENO_RTT) {
                    uint64_t min_win;

                    if (path_x->rtt_min > PICOQUIC_TARGET_SATELLITE_RTT) {
                        min_win = (uint64_t)((double)PICOQUIC_CWIN_INITIAL * (double)PICOQUIC_TARGET_SATELLITE_RTT / (double)PICOQUIC_TARGET_RENO_RTT);
                    }
                    else {
                        /* Increase initial CWIN for long delay links. */
                        min_win = (uint64_t)((double)PICOQUIC_CWIN_INITIAL * (double)path_x->rtt_min / (double)PICOQUIC_TARGET_RENO_RTT);
                    }
                    if (min_win > nr_state->nrss.cwin) {
                        nr_state->nrss.cwin = min_win;
                        path_x->cwin = min_win;
                    }
                }

                if (picoquic_hystart_test(&nr_state->rtt_filter, (cnx->is_time_stamp_enabled) ? one_way_delay : rtt_measurement,
                    cnx->path[0]->pacing_packet_time_microsec, current_time,
                    cnx->is_time_stamp_enabled)) {
                    /* RTT increased too much, get out of slow start! */
                    nr_state->nrss.ssthresh = nr_state->nrss.cwin;
                    nr_state->nrss.alg_state = picoquic_newreno_alg_congestion_avoidance;
                    path_x->cwin = nr_state->nrss.cwin;
                    path_x->is_ssthresh_initialized = 1;
                }
            }
            break;
        case picoquic_congestion_notification_cwin_blocked:
            break;
        case picoquic_congestion_notification_bw_measurement:
            if (nr_state->nrss.alg_state == picoquic_newreno_alg_slow_start &&
                nr_state->nrss.ssthresh == UINT64_MAX) {
                /* RTT measurements will happen after the bandwidth is estimated */
                uint64_t max_win = path_x->max_bandwidth_estimate * path_x->smoothed_rtt / 1000000;
                uint64_t min_win = max_win /= 2;
                if (nr_state->nrss.cwin < min_win) {
                    nr_state->nrss.cwin = min_win;
                    path_x->cwin = min_win;
                }
            }
            break;
        case picoquic_congestion_notification_reset:
            picoquic_no_cc_reset(nr_state, path_x);
            break;
        default:
            /* ignore */
            break;
        }
        nr_state->nrss.cwin = 62500;
        path_x->cwin = 62500;
        /* Compute pacing data */
        picoquic_update_pacing_data(cnx, path_x, nr_state->nrss.alg_state == picoquic_newreno_alg_slow_start &&
            nr_state->nrss.ssthresh == 62500);
    }
}

/* Release the state of the congestion control algorithm */
static void picoquic_no_cc_delete(picoquic_path_t* path_x)
{
    if (path_x->congestion_alg_state != NULL) {
        free(path_x->congestion_alg_state);
        path_x->congestion_alg_state = NULL;
    }
}

/* Observe the state of congestion control */

void picoquic_no_cc_observe(picoquic_path_t* path_x, uint64_t* cc_state, uint64_t* cc_param)
{
    picoquic_no_cc_state_t* nr_state = (picoquic_no_cc_state_t*)path_x->congestion_alg_state;
    *cc_state = (uint64_t)nr_state->nrss.alg_state;
    *cc_param = (nr_state->nrss.ssthresh == 62500) ? 0 : nr_state->nrss.ssthresh;
}

/* Definition record for the New Reno algorithm */

#define PICOQUIC_NO_CC_ID "nocc" /* NR88 */

picoquic_congestion_algorithm_t picoquic_no_cc_algorithm_struct = {
    PICOQUIC_NO_CC_ID, PICOQUIC_CC_ALGO_NUMBER_NO_CC,
    picoquic_no_cc_init,
    picoquic_no_cc_notify,
    picoquic_no_cc_delete,
    picoquic_no_cc_observe
};

picoquic_congestion_algorithm_t* picoquic_no_cc_algorithm = &picoquic_no_cc_algorithm_struct;
