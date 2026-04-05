// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

#pragma once

extern "C" {

// ── l_io (fd event watches) ──

struct l_io;

typedef bool (*l_io_read_cb_t)(struct l_io *io, void *user_data);
typedef bool (*l_io_write_cb_t)(struct l_io *io, void *user_data);
typedef void (*l_io_disconnect_cb_t)(struct l_io *io, void *user_data);
typedef void (*l_io_destroy_cb_t)(void *user_data);

struct l_io *l_io_new(int fd);
void l_io_destroy(struct l_io *io);
int l_io_get_fd(struct l_io *io);
bool l_io_set_read_handler(struct l_io *io, l_io_read_cb_t callback,
                           void *user_data, l_io_destroy_cb_t destroy);
bool l_io_set_write_handler(struct l_io *io, l_io_write_cb_t callback,
                            void *user_data, l_io_destroy_cb_t destroy);
bool l_io_set_disconnect_handler(struct l_io *io, l_io_disconnect_cb_t callback,
                                 void *user_data, l_io_destroy_cb_t destroy);

// ── l_idle (deferred one-shot callbacks) ──

typedef void (*l_idle_oneshot_cb_t)(void *user_data);
typedef void (*l_idle_destroy_cb_t)(void *user_data);

void l_idle_oneshot(l_idle_oneshot_cb_t callback, void *user_data,
                    l_idle_destroy_cb_t destroy);

// ── l_timeout (timers) ──

struct l_timeout;

typedef void (*l_timeout_notify_cb_t)(struct l_timeout *timeout, void *user_data);
typedef void (*l_timeout_destroy_cb_t)(void *user_data);

struct l_timeout *l_timeout_create_ms(unsigned long milliseconds,
                                      l_timeout_notify_cb_t callback,
                                      void *user_data,
                                      l_timeout_destroy_cb_t destroy);
void l_timeout_modify_ms(struct l_timeout *timeout, unsigned long milliseconds);
void l_timeout_remove(struct l_timeout *timeout);

} // extern "C"
