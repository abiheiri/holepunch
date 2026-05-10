/*
 * Copyright (c) 2026 Al Biheiri <al@forgottheaddress.com>
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#include "upnp.h"
#include "update.h"
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t should_exit = 0;
static char pidfile_path[256] = {0};
static int bg_mode = 0;

static void log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (bg_mode) {
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        syslog(LOG_INFO, "%s", buf);
    } else {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }
    va_end(ap);
}

static void signal_handler(int sig)
{
    (void)sig;
    should_exit = 1;
}

static void write_pidfile(int dport, const char *proto)
{
    snprintf(pidfile_path, sizeof(pidfile_path),
             "/tmp/punch-%d-%s.pid", dport, proto);
    FILE *f = fopen(pidfile_path, "w");
    if (f) {
        fprintf(f, "%d\n", (int)getpid());
        fclose(f);
    }
}

static void remove_pidfile(void)
{
    if (pidfile_path[0]) {
        unlink(pidfile_path);
        pidfile_path[0] = '\0';
    }
}

static char *detect_target(void)
{
    struct utsname u;
    if (uname(&u) != 0) return NULL;
#ifdef __APPLE__
    const char *suffix = "apple-darwin";
#else
    const char *suffix = "unknown-linux-gnu";
#endif
    const char *machine = u.machine;
    if (strcmp(machine, "arm64") == 0) machine = "aarch64";
    size_t n = strlen(machine) + strlen(suffix) + 2;
    char *t = malloc(n);
    if (t) {
        snprintf(t, n, "%s-%s", machine, suffix);
    }
    return t;
}

static int is_cgnat(const char *ip)
{
    if (strncmp(ip, "100.", 4) != 0) return 0;
    int b = atoi(ip + 4);
    return b >= 64 && b <= 127;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s -sport <local_port> -dport <external_port> [options]\n", prog);
    printf("\nRequired arguments:\n");
    printf("  -sport    Local port where the service is listening\n");
    printf("  -dport    External port to open on the router\n");
    printf("\nOptional arguments:\n");
    printf("  -proto    Protocol: tcp (default), udp, or both\n");
    printf("  -lease    Lease duration in seconds (default: 1800)\n");
    printf("  -background   Daemonize the process\n");
    printf("  -kill     Stop a background instance for the given -dport\n");
    printf("  -local-ip Override auto-detected LAN IP\n");
    printf("  -verbose  Print detailed UPnP logs\n");
    printf("  --version Print version information\n");
    printf("  --list    List current UPnP port mappings\n");
    printf("  --update  Update to the latest release\n");
}

static int do_kill(int dport, const char *proto)
{
    char pidfile[256];
    snprintf(pidfile, sizeof(pidfile), "/tmp/punch-%d-%s.pid", dport, proto);
    FILE *f = fopen(pidfile, "r");
    if (!f) {
        fprintf(stderr, "No background process found for port %d/%s\n", dport, proto);
        return 1;
    }
    int pid;
    if (fscanf(f, "%d", &pid) != 1) {
        fclose(f);
        return 1;
    }
    fclose(f);

    if (kill(pid, SIGTERM) != 0) {
        perror("kill");
        return 1;
    }

    for (int i = 0; i < 50; i++) {
        if (kill(pid, 0) != 0) break;
        usleep(100000);
    }

    unlink(pidfile);
    return 0;
}

int main(int argc, char **argv)
{
    const char *version = VERSION;
    int local_port = -1;
    int external_port = -1;
    const char *proto = "TCP";
    int do_both = 0;
    int lease_duration = 1800;
    int background = 0;
    int kill_mode = 0;
    const char *local_ip_override = NULL;
    int verbose = 0;
    int do_update = 0;
    int do_list = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("punch %s\n", version);
            return 0;
        } else if (strcmp(argv[i], "--update") == 0) {
            do_update = 1;
        } else if (strcmp(argv[i], "--list") == 0) {
            do_list = 1;
        } else if (strcmp(argv[i], "-sport") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing value for -sport\n"); return 2; }
            local_port = atoi(argv[i]);
        } else if (strcmp(argv[i], "-dport") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing value for -dport\n"); return 2; }
            external_port = atoi(argv[i]);
        } else if (strcmp(argv[i], "-proto") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing value for -proto\n"); return 2; }
            if (strcmp(argv[i], "tcp") == 0) {
                proto = "TCP";
            } else if (strcmp(argv[i], "udp") == 0) {
                proto = "UDP";
            } else if (strcmp(argv[i], "both") == 0) {
                proto = "both";
                do_both = 1;
            } else {
                fprintf(stderr, "Invalid protocol: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "-lease") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing value for -lease\n"); return 2; }
            lease_duration = atoi(argv[i]);
        } else if (strcmp(argv[i], "-background") == 0) {
            background = 1;
        } else if (strcmp(argv[i], "-kill") == 0) {
            kill_mode = 1;
        } else if (strcmp(argv[i], "-local-ip") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing value for -local-ip\n"); return 2; }
            local_ip_override = argv[i];
        } else if (strcmp(argv[i], "-verbose") == 0) {
            verbose = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (do_update) {
        char *target = detect_target();
        int rc = self_update(version, target ? target : "unknown");
        free(target);
        return rc;
    }

    if (do_list) {
        upnp_ctx_t ctx = {0};
        ctx.verbose = verbose;
        if (upnp_discover(&ctx) != 0) return 3;
        upnp_list_mappings(&ctx);
        upnp_cleanup(&ctx);
        return 0;
    }

    if (kill_mode) {
        if (external_port < 0) {
            fprintf(stderr, "-kill requires -dport\n");
            return 2;
        }
        return do_kill(external_port, proto);
    }

    if (local_port < 0 || external_port < 0) {
        fprintf(stderr, "Both -sport and -dport are required.\n");
        print_usage(argv[0]);
        return 2;
    }

    if (local_port <= 0 || local_port > 65535 || external_port <= 0 || external_port > 65535) {
        fprintf(stderr, "Invalid port numbers. Ports must be in range 1-65535.\n");
        return 2;
    }

    if (local_port == 22 || local_port == 23 || local_port == 3389) {
        fprintf(stderr, "Warning: Exposing service on port %d without proper authentication is dangerous.\n",
                local_port);
    }

    upnp_ctx_t ctx = {0};
    ctx.verbose = verbose;

    if (upnp_discover(&ctx) != 0) {
        return 3;
    }

    if (local_ip_override) {
        strncpy(ctx.local_ip, local_ip_override, sizeof(ctx.local_ip) - 1);
    }

    if (verbose) {
        log_msg("External IP: %s", ctx.external_ip[0] ? ctx.external_ip : "unknown");
    }

    if (ctx.external_ip[0] && is_cgnat(ctx.external_ip)) {
        log_msg("Warning: Router WAN IP is in CGNAT range (%s). The port may not be reachable from the public internet.",
                ctx.external_ip);
    }

    if (upnp_add_mapping(&ctx, local_port, external_port, "TCP", lease_duration) != 0) {
        upnp_cleanup(&ctx);
        return 4;
    }
    if (do_both) {
        if (upnp_add_mapping(&ctx, local_port, external_port, "UDP", lease_duration) != 0) {
            upnp_remove_mapping(&ctx, external_port, "TCP");
            upnp_cleanup(&ctx);
            return 4;
        }
    }

    log_msg("Mapped external port %d -> %s:%d (%s lease %ds)",
            external_port, ctx.local_ip, local_port,
            do_both ? "TCP+UDP" : proto, lease_duration);

    if (ctx.external_ip[0]) {
        log_msg("Router WAN IP: %s", ctx.external_ip);
    }

    if (background) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            upnp_cleanup(&ctx);
            return 1;
        }
        if (pid > 0) {
            /* Parent exits after fork succeeds */
            upnp_cleanup(&ctx);
            return 0;
        }
        /* Child process */
        if (setsid() < 0) {
            perror("setsid");
            upnp_cleanup(&ctx);
            return 1;
        }
        bg_mode = 1;
        openlog("punch", LOG_PID, LOG_DAEMON);
        if (freopen("/dev/null", "r", stdin) == NULL) { /* ignore */ }
        if (freopen("/dev/null", "w", stdout) == NULL) { /* ignore */ }
        if (freopen("/dev/null", "w", stderr) == NULL) { /* ignore */ }
    }

    write_pidfile(external_port, proto);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int lease_half = lease_duration / 2;
    if (lease_half < 1) lease_half = 1;

    while (!should_exit) {
        sleep(lease_half);
        if (should_exit) break;

        int retries = 0;
        while (retries < 3) {
            int ok = (upnp_add_mapping(&ctx, local_port, external_port, "TCP", lease_duration) == 0);
            if (do_both) {
                ok = ok && (upnp_add_mapping(&ctx, local_port, external_port, "UDP", lease_duration) == 0);
            }
            if (ok) {
                if (verbose) log_msg("Lease renewed.");
                break;
            }
            retries++;
            if (retries >= 3) {
                log_msg("Renewal failed after 3 attempts. Exiting.");
                should_exit = 1;
                break;
            }
            int delay = 1 << (retries - 1);
            sleep(delay);
            if (should_exit) break;
        }
    }

    log_msg("Cleaning up port mapping...");
    upnp_remove_mapping(&ctx, external_port, "TCP");
    if (do_both) {
        upnp_remove_mapping(&ctx, external_port, "UDP");
    }
    upnp_cleanup(&ctx);
    remove_pidfile();

    if (bg_mode) {
        closelog();
    }

    return 0;
}
