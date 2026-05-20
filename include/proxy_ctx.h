/*
 * MIT License
 * Copyright (c) 2025 Jonny Röker
 *
 * File: proxy_ctx.h
 * Gemeinsamer Laufzeit-Kontext, der an alle Threads und die Konsole
 * weitergereicht wird.
 */

#ifndef PROXY_CTX_H
#define PROXY_CTX_H

#include <stdint.h>
#include "param_cache.h"

typedef struct {
    int             serial_fd;  /* Serieller File-Deskriptor (USB → FC)  */
    int             udp_fd;     /* UDP-Socket (Netzwerk → GCS)           */
    param_cache_t  *cache;      /* MAVLink-Parameter-Cache               */
    volatile int    running;    /* 0 → alle Threads sauber beenden       */
} proxy_ctx_t;

#endif /* PROXY_CTX_H */
