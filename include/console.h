/*
 * MIT License
 * Copyright (c) 2025 Jonny Röker
 *
 * File: console.h
 * Interaktive readline-Konsole mit Tab-Vervollständigung.
 * Wird nur im Modus MODE_CONSOLE gestartet.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include "proxy_ctx.h"

/*
 * Startet die interaktive Konsole (blockiert den aufrufenden Thread,
 * typisch den Main-Thread, bis der Benutzer "quit" eingibt).
 * Setzt ctx->running = 0 beim Beenden.
 */
void console_run(proxy_ctx_t *ctx);

#endif /* CONSOLE_H */
