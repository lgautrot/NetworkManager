/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

#pragma once

/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include "nm-sd-adapt.h"

#include <stdbool.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "time-util.h"

int flush_fd(int fd);

ssize_t loop_read(int fd, void *buf, size_t nbytes, bool do_poll);
int loop_read_exact(int fd, void *buf, size_t nbytes, bool do_poll);
int loop_write(int fd, const void *buf, size_t nbytes, bool do_poll);

int pipe_eof(int fd);

int fd_wait_for_event(int fd, int event, usec_t timeout);

ssize_t sparse_write(int fd, const void *p, size_t sz, size_t run_length);

#define IOVEC_SET_STRING(i, s)                  \
        do {                                    \
                struct iovec *_i = &(i);        \
                char *_s = (char *)(s);         \
                _i->iov_base = _s;              \
                _i->iov_len = strlen(_s);       \
        } while(false)

static inline size_t IOVEC_TOTAL_SIZE(const struct iovec *i, unsigned n) {
        unsigned j;
        size_t r = 0;

        for (j = 0; j < n; j++)
                r += i[j].iov_len;

        return r;
}

static inline size_t IOVEC_INCREMENT(struct iovec *i, unsigned n, size_t k) {
        unsigned j;

        for (j = 0; j < n; j++) {
                size_t sub;

                if (_unlikely_(k <= 0))
                        break;

                sub = MIN(i[j].iov_len, k);
                i[j].iov_len -= sub;
                i[j].iov_base = (uint8_t*) i[j].iov_base + sub;
                k -= sub;
        }

        return k;
}
