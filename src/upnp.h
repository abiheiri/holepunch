/*
 * Copyright (c) 2026 Al Biheiri <al@forgottheaddress.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef UPNP_H
#define UPNP_H

#include <stddef.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

typedef struct {
    int verbose;
    struct UPNPDev *devlist;
    struct UPNPUrls urls;
    struct IGDdatas data;
    char local_ip[64];
    char external_ip[64];
    int discovered;
} upnp_ctx_t;

int upnp_discover(upnp_ctx_t *ctx);
int upnp_add_mapping(upnp_ctx_t *ctx, int local_port, int external_port,
                     const char *proto, int lease_duration);
int upnp_remove_mapping(upnp_ctx_t *ctx, int external_port, const char *proto);
void upnp_list_mappings(upnp_ctx_t *ctx);
void upnp_cleanup(upnp_ctx_t *ctx);

#endif
