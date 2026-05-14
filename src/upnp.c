/*
 * Copyright (c) 2026 Al Biheiri <al@forgottheaddress.com>
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#include "upnp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int upnp_discover(upnp_ctx_t *ctx)
{
    int error = 0;
    ctx->devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &error);
    if (!ctx->devlist) {
        fprintf(stderr, "No UPnP IGD found. Please enable UPnP on your router.\n");
        return -1;
    }

#ifndef MINIUPNPC_API_VERSION
#define MINIUPNPC_API_VERSION 0
#endif

#if MINIUPNPC_API_VERSION >= 18
    int r = UPNP_GetValidIGD(ctx->devlist, &ctx->urls, &ctx->data,
                             ctx->local_ip, sizeof(ctx->local_ip),
                             NULL, 0);
#else
    int r = UPNP_GetValidIGD(ctx->devlist, &ctx->urls, &ctx->data,
                             ctx->local_ip, sizeof(ctx->local_ip));
#endif
    if (r != 1 && r != 2) {
        fprintf(stderr, "No UPnP IGD found. Please enable UPnP on your router.\n");
        freeUPNPDevlist(ctx->devlist);
        ctx->devlist = NULL;
        return -1;
    }

    ctx->discovered = 1;

    if (ctx->verbose) {
        fprintf(stderr, "UPnP IGD discovered at %s\n", ctx->urls.controlURL);
        fprintf(stderr, "Local IP: %s\n", ctx->local_ip);
    }

    /* Fetch external IP */
    char wan_addr[64] = {0};
    if (UPNP_GetExternalIPAddress(ctx->urls.controlURL,
                                  ctx->data.first.servicetype,
                                  wan_addr) == UPNPCOMMAND_SUCCESS) {
        snprintf(ctx->external_ip, sizeof(ctx->external_ip), "%s", wan_addr);
    }

    return 0;
}

int upnp_add_mapping(const upnp_ctx_t *ctx, int local_port, int external_port,
                     const char *proto, int lease_duration)
{
    if (!ctx->discovered) return -1;

    char eport[16], iport[16], duration[16];
    snprintf(eport, sizeof(eport), "%d", external_port);
    snprintf(iport, sizeof(iport), "%d", local_port);
    snprintf(duration, sizeof(duration), "%d", lease_duration);

    int r = UPNP_AddPortMapping(ctx->urls.controlURL,
                                ctx->data.first.servicetype,
                                eport, iport,
                                ctx->local_ip,
                                "holepunch",
                                proto,
                                NULL,
                                duration);
    if (r != UPNPCOMMAND_SUCCESS) {
        if (r == 718) {
            fprintf(stderr, "Port %d is already forwarded by another rule.\n",
                    external_port);
        } else {
            fprintf(stderr, "Failed to add port mapping (error %d).\n", r);
        }
        return -1;
    }

    if (ctx->verbose) {
        fprintf(stderr, "Added %s mapping: external %s -> %s:%s (lease %ss)\n",
                proto, eport, ctx->local_ip, iport, duration);
    }
    return 0;
}

int upnp_remove_mapping(const upnp_ctx_t *ctx, int external_port, const char *proto)
{
    if (!ctx->discovered) return -1;

    char eport[16];
    snprintf(eport, sizeof(eport), "%d", external_port);

    int r = UPNP_DeletePortMapping(ctx->urls.controlURL,
                                   ctx->data.first.servicetype,
                                   eport, proto, NULL);
    if (r != UPNPCOMMAND_SUCCESS && ctx->verbose) {
        fprintf(stderr, "Warning: failed to delete port mapping (error %d).\n", r);
    }
    return (r == UPNPCOMMAND_SUCCESS) ? 0 : -1;
}

void upnp_list_mappings(const upnp_ctx_t *ctx)
{
    if (!ctx->discovered) return;

    int i = 0;
    int printed_header = 0;
    char index[16];
    char extPort[8];
    char intClient[64];
    char intPort[8];
    char protocol[8];
    char desc[128];
    char enabled[8];
    char rHost[64];
    char duration[16];

    for (;;) {
        snprintf(index, sizeof(index), "%d", i);
        int r = UPNP_GetGenericPortMappingEntry(ctx->urls.controlURL,
                                                ctx->data.first.servicetype,
                                                index,
                                                extPort, intClient, intPort,
                                                protocol, desc, enabled,
                                                rHost, duration);
        if (r != UPNPCOMMAND_SUCCESS) break;

        if (!printed_header) {
            printf("%-6s %-8s %-8s %-20s %-8s %-12s %s\n",
                   "Index", "Proto", "ExtPort", "IntClient", "IntPort", "Lease", "Description");
            printed_header = 1;
        }
        printf("%-6d %-8s %-8s %-20s %-8s %-12s %s\n",
               i, protocol, extPort, intClient, intPort, duration, desc);
        i++;
    }

    if (!printed_header) {
        printf("No port mappings found on this router.\n");
    }
}

void upnp_cleanup(upnp_ctx_t *ctx)
{
    if (ctx->discovered) {
        FreeUPNPUrls(&ctx->urls);
        ctx->discovered = 0;
    }
    if (ctx->devlist) {
        freeUPNPDevlist(ctx->devlist);
        ctx->devlist = NULL;
    }
}
