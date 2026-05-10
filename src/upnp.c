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

    int r = UPNP_GetValidIGD(ctx->devlist, &ctx->urls, &ctx->data,
                             ctx->local_ip, sizeof(ctx->local_ip),
                             ctx->external_ip, sizeof(ctx->external_ip));
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

    /* Fetch external IP if not already populated by GetValidIGD */
    if (ctx->external_ip[0] == '\0') {
        char wan_addr[64] = {0};
        if (UPNP_GetExternalIPAddress(ctx->urls.controlURL,
                                      ctx->data.first.servicetype,
                                      wan_addr) == UPNPCOMMAND_SUCCESS) {
            strncpy(ctx->external_ip, wan_addr, sizeof(ctx->external_ip) - 1);
        }
    }

    return 0;
}

int upnp_add_mapping(upnp_ctx_t *ctx, int local_port, int external_port,
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

int upnp_remove_mapping(upnp_ctx_t *ctx, int external_port, const char *proto)
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
