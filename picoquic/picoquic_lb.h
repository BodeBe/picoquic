/*
* Author: Christian Huitema
* Copyright (c) 2021, Private Octopus, Inc.
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

#ifndef PICOQUIC_LB_H
#define PICOQUIC_LB_H

#include "picoquic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load balancer support is defined in https://datatracker.ietf.org/doc/draft-ietf-quic-load-balancers/
 * The draft defines methods for encoding a server ID in a connection identifier, and optionally
 * obfuscating or encrypting the CID value. The CID are generated by the individual servers,
 * based on configuration options provided by the load balancer. The draft also defines
 * methods for generating retry tokens either by a protection box colocated with the
 * load balancer, or at the individual server, with methods for letting individual
 * servers retrieve information from the tokens.
 * The configuration options are encoded in the picoquic_load_balancer_config_t structure.
 */

typedef enum {
    picoquic_load_balancer_cid_clear,
    picoquic_load_balancer_cid_obfuscated,
    picoquic_load_balancer_cid_stream_cipher,
    picoquic_load_balancer_cid_block_cipher
} picoquic_load_balancer_cid_method_enum;


typedef struct st_picoquic_load_balancer_config_t {
    picoquic_load_balancer_cid_method_enum method;
    uint8_t server_id_length;
    uint8_t routing_bits_length; /* Used in divider mode */
    uint8_t nonce_length; /* used in stream cipher mode */
    uint8_t zero_pad_length; /* used in block cipher mode */
    uint8_t connection_id_length;
    uint8_t first_byte;
    uint64_t server_id64;
    uint8_t cid_encryption_key[16];
    uint64_t divider; /* used in obfuscation methods */
} picoquic_load_balancer_config_t;

int picoquic_lb_compat_cid_config(picoquic_quic_t* quic, picoquic_load_balancer_config_t* lb_config);
void picoquic_lb_compat_cid_config_free(picoquic_quic_t* quic);

typedef struct st_picoquic_load_balancer_cid_context_t {
    picoquic_load_balancer_cid_method_enum method;
    uint8_t server_id_length;
    uint8_t routing_bits_length;
    uint8_t nonce_length; /* used in stream cipher mode */
    uint8_t zero_pad_length; /* used in block cipher mode */
    uint8_t connection_id_length;
    uint8_t first_byte;
    uint64_t server_id64;
    uint64_t divider; /* used in obfuscation methods */
    uint8_t server_id[16];
    void* cid_encryption_context; /* used in stream and cipher mode */
    void* cid_decryption_context; /* used in block cipher mode */
} picoquic_load_balancer_cid_context_t;

void picoquic_lb_compat_cid_generate(picoquic_quic_t* quic, picoquic_connection_id_t cnx_id_local, picoquic_connection_id_t cnx_id_remote, void* cnx_id_cb_data, picoquic_connection_id_t* cnx_id_returned);
uint64_t picoquic_lb_compat_cid_verify(picoquic_quic_t* quic, void* cnx_id_cb_data, picoquic_connection_id_t const* cnx_id);
#ifdef __cplusplus
}
#endif

#endif /* PICOQUIC_LB_H */
