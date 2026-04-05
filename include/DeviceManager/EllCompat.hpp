// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// Thin C++ forward declarations for the ELL functions used by DeviceManager.
// Avoids including <ell/ell.h> which contains C-only constructs
// (DEFINE_CLEANUP_FUNC void* casts, [static N] array parameters) that
// don't compile under -pedantic C++.

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
