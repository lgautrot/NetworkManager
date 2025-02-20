/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

#pragma once

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

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

#include "sd-event.h"

#include "util.h"

DEFINE_TRIVIAL_CLEANUP_FUNC(sd_event*, sd_event_unref);
DEFINE_TRIVIAL_CLEANUP_FUNC(sd_event_source*, sd_event_source_unref);

#define _cleanup_event_unref_ _cleanup_(sd_event_unrefp)
#define _cleanup_event_source_unref_ _cleanup_(sd_event_source_unrefp)
