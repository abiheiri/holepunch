/*
 * Copyright (c) 2026 Al Biheiri <al@forgottheaddress.com>
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#include "update.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static int version_compare(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    for (;;) {
        long av = 0, bv = 0;
        while (*a >= '0' && *a <= '9') { av = av * 10 + (*a - '0'); a++; }
        while (*b >= '0' && *b <= '9') { bv = bv * 10 + (*b - '0'); b++; }
        if (av != bv) return (av < bv) ? -1 : 1;
        if (*a != '.' && *b != '.') return 0;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
}

static char *shell_read(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    char *out = NULL;
    size_t cap = 0, len = 0;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (len + n + 1 > cap) {
            cap = cap ? cap * 2 : 65536;
            out = realloc(out, cap);
            if (!out) { pclose(fp); return NULL; }
        }
        memcpy(out + len, buf, n);
        len += n;
    }
    pclose(fp);
    if (out) out[len] = '\0';
    return out;
}

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static char *json_str(const char **pp)
{
    const char *p = *pp;
    if (*p != '"') return NULL;
    p++;
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) p += 2;
        else p++;
    }
    size_t n = p - start;
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, start, n);
    out[n] = '\0';
    if (*p == '"') p++;
    *pp = p;
    return out;
}

static int skip_val(const char **pp)
{
    const char *p = skip_ws(*pp);
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\' && *(p + 1)) p += 2;
            else p++;
        }
        if (*p == '"') p++;
    } else if (*p == '{') {
        p++;
        while (*p) {
            p = skip_ws(p);
            if (*p == '}') { p++; break; }
            if (skip_val(&p) < 0) return -1;
            p = skip_ws(p);
            if (*p == ':') p++;
            if (skip_val(&p) < 0) return -1;
            p = skip_ws(p);
            if (*p == ',') p++;
        }
    } else if (*p == '[') {
        p++;
        while (*p) {
            p = skip_ws(p);
            if (*p == ']') { p++; break; }
            if (skip_val(&p) < 0) return -1;
            p = skip_ws(p);
            if (*p == ',') p++;
        }
    } else {
        while (*p && *p != ',' && *p != ']' && *p != '}') p++;
    }
    *pp = p;
    return 0;
}

static char *find_asset_url(const char *json, const char *expected_name)
{
    const char *assets = strstr(json, "\"assets\"");
    if (!assets) return NULL;
    const char *p = assets + 8;
    p = skip_ws(p);
    if (*p != ':') return NULL;
    p++;
    p = skip_ws(p);
    if (*p != '[') return NULL;
    p++;

    while (*p) {
        p = skip_ws(p);
        if (*p == ']') break;
        if (*p != '{') {
            if (skip_val(&p) < 0) return NULL;
            p = skip_ws(p);
            if (*p == ',') p++;
            continue;
        }
        p++;

        char *name = NULL;
        char *url = NULL;

        while (*p) {
            p = skip_ws(p);
            if (*p == '}') { p++; break; }
            if (*p != '"') {
                if (skip_val(&p) < 0) { free(name); free(url); return NULL; }
                p = skip_ws(p);
                if (*p == ',') p++;
                continue;
            }
            char *key = json_str(&p);
            p = skip_ws(p);
            if (*p == ':') p++;
            if (key && strcmp(key, "name") == 0) {
                free(name);
                p = skip_ws(p);
                name = json_str(&p);
            } else if (key && strcmp(key, "browser_download_url") == 0) {
                free(url);
                p = skip_ws(p);
                url = json_str(&p);
            } else {
                if (skip_val(&p) < 0) { free(key); free(name); free(url); return NULL; }
            }
            free(key);
            p = skip_ws(p);
            if (*p == ',') p++;
        }

        if (name && url && strcmp(name, expected_name) == 0) {
            free(name);
            return url;
        }
        free(name);
        free(url);

        p = skip_ws(p);
        if (*p == ',') p++;
    }
    return NULL;
}

static char *get_tag_name(const char *json)
{
    const char *p = strstr(json, "\"tag_name\"");
    if (!p) return NULL;
    p += 10;
    p = skip_ws(p);
    if (*p != ':') return NULL;
    p++;
    p = skip_ws(p);
    return json_str(&p);
}

int self_update(const char *current_version, const char *target_triple)
{
    char api_url[512];
    snprintf(api_url, sizeof(api_url),
             "https://api.github.com/repos/%s/%s/releases/latest",
             UPDATE_REPO_OWNER, UPDATE_REPO_NAME);

    printf("Checking for latest release...\n");
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -fsL -H \"User-Agent: %s/%s\" \"%s\" 2>/dev/null",
             UPDATE_BINARY_NAME, current_version, api_url);
    char *body = shell_read(cmd);
    if (!body || strlen(body) == 0) {
        printf("No updates available.\n");
        free(body);
        return 0;
    }

    char *latest = get_tag_name(body);
    if (!latest) {
        fprintf(stderr, "Failed to parse release info\n");
        free(body);
        return -1;
    }

    if (version_compare(latest, current_version) <= 0) {
        printf("Already up to date (%s).\n", current_version);
        free(latest);
        free(body);
        return 0;
    }

    char expected_name[256];
    snprintf(expected_name, sizeof(expected_name),
             "%s-%s.tar.gz", UPDATE_BINARY_NAME, target_triple);

    char *download_url = find_asset_url(body, expected_name);
    free(body);

    if (!download_url) {
        fprintf(stderr, "Could not find asset %s in latest release\n", expected_name);
        free(latest);
        return -1;
    }

    printf("Downloading %s ...\n", download_url);
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/%s-update-%d",
             UPDATE_BINARY_NAME, (int)getpid());
    if (mkdir(tmp_dir, 0755) != 0 && errno != EEXIST) {
        free(download_url);
        free(latest);
        return -1;
    }

    char tar_path[1024];
    snprintf(tar_path, sizeof(tar_path), "%s/update.tar.gz", tmp_dir);
    snprintf(cmd, sizeof(cmd),
             "curl -fsL -H \"User-Agent: %s/%s\" -o \"%s\" \"%s\" 2>/dev/null",
             UPDATE_BINARY_NAME, current_version, tar_path, download_url);
    free(download_url);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Download failed\n");
        free(latest);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\"", tar_path, tmp_dir);
    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Extract failed\n");
        free(latest);
        return -1;
    }

    char new_exe[1024];
    snprintf(new_exe, sizeof(new_exe), "%s/%s", tmp_dir, UPDATE_BINARY_NAME);
    struct stat st;
    if (stat(new_exe, &st) != 0) {
        fprintf(stderr, "Update archive did not contain expected binary\n");
        free(latest);
        return -1;
    }

    char current_exe[1024];
    ssize_t len = readlink("/proc/self/exe", current_exe, sizeof(current_exe) - 1);
    if (len < 0) {
#ifdef __APPLE__
        uint32_t size = sizeof(current_exe);
        if (_NSGetExecutablePath(current_exe, &size) != 0) {
            fprintf(stderr, "Cannot determine current executable path\n");
            free(latest);
            return -1;
        }
#else
        fprintf(stderr, "Cannot determine current executable path\n");
        free(latest);
        return -1;
#endif
    } else {
        current_exe[len] = '\0';
    }

    char backup[1024 + 8];
    snprintf(backup, sizeof(backup), "%s.old", current_exe);
    unlink(backup);
    if (rename(current_exe, backup) != 0) {
        fprintf(stderr, "Failed to backup current binary\n");
        free(latest);
        return -1;
    }
    if (link(new_exe, current_exe) != 0) {
        if (rename(backup, current_exe) != 0) {
            fprintf(stderr, "Failed to restore backup\n");
        }
        free(latest);
        return -1;
    }
    chmod(current_exe, 0755);
    unlink(backup);

    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", tmp_dir);
    int cleanup_rc = system(cmd);
    (void)cleanup_rc;

    printf("Updated to %s successfully.\n", latest);
    free(latest);
    return 0;
}
