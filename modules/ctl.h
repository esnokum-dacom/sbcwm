#pragma once

#include <stddef.h>

const char *ctl_socket_path(char *buf, size_t n);

void ctl_init(void);
int  ctl_fd(void);
void ctl_accept(void);
void ctl_cleanup(void);

void ctl_handle(int fd, const char *line);
