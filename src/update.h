#ifndef UPDATE_H
#define UPDATE_H

#define UPDATE_REPO_OWNER "abiheiri"
#define UPDATE_REPO_NAME  "holepunch"
#define UPDATE_BINARY_NAME "punch"

/* Self-update from GitHub releases. Returns 0 on success. */
int self_update(const char *current_version, const char *target_triple);

#endif
