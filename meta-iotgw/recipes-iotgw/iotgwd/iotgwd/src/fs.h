#pragma once
#include <stddef.h>

// List *.yaml in a directory. Returns number of entries put in 'out' (<= max)
// Returns 0 if dir missing/empty; negative on error. Paths are absolute.
int fs_list_yaml(const char *dirpath, char out[][512], size_t max);

// Read full file into buffer (truncates if too long). Returns bytes copied or <0 on error.
int fs_read_file(const char *path, char *buf, size_t buflen);
