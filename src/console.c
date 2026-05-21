/*
 * MIT License
 * Copyright (c) 2025 Jonny Roeker
 *
 * File: console.c
 *
 * Interaktive readline-Konsole für den MAVLink-Proxy.
 *
 * Features:
 *   - Tab-Vervollständigung für Befehle und Parameternamen (wie Bash)
 *   - Eingabe-Historie (Pfeiltasten hoch/runter)
 *   - Befehle: list [filter], get <name>, set <name> <wert>,
 *              refresh, status, help, quit
 *
 * Voraussetzung: libreadline  →  sudo apt install libreadline-dev
 */

#define _GNU_SOURCE   /* strcasestr, strtok_r */

#include "console.h"
#include "param_cache.h"
#include "serial.h"
#include "logging.h"
#include "common/mavlink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>

#include <readline/readline.h>
#include <readline/history.h>

/* ─── ANSI-Farben ─────────────────────────────────────────────────── */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"

/* Pfad zur gespeicherten Eingabe-Historie */
#define HISTORY_FILE  ".mavconsole_history"

/* ─── Globaler Kontext für readline-Callbacks ─────────────────────── */
static proxy_ctx_t *g_ctx = NULL;

/* ─── MAVLink-Hilfsfunktionen ─────────────────────────────────────── */

static void mav_send_param_request_list(int serial_fd) {
    mavlink_message_t msg;
    mavlink_msg_param_request_list_pack(
            255, 190, /* GCS sysid / compid */
            &msg,
            1, 1 /* Ziel: Autopilot sysid / compid */
            );
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buf, &msg);
    writeSerial(serial_fd, buf, len);
}

static bool mav_send_param_set(int serial_fd,
        const char *name,
        float value,
        uint8_t type) {
    mavlink_message_t msg;
    mavlink_msg_param_set_pack(
            255, 190,
            &msg,
            1, 1,
            name, value, type
            );
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buf, &msg);
    return writeSerial(serial_fd, buf, len) == len;
}

/* ─── Tab-Completion ──────────────────────────────────────────────── */

static const char *cmd_table[] = {
    "list", "get", "set", "refresh", "status", "help", "quit", NULL
};

static char *generator_command(const char *text, int state) {
    static int idx;
    if (state == 0) idx = 0;
    size_t len = strlen(text);
    while (cmd_table[idx]) {
        const char *c = cmd_table[idx++];
        if (strncasecmp(c, text, len) == 0)
            return strdup(c);
    }
    return NULL;
}

static char *generator_param(const char *text, int state) {
    if (!g_ctx || !g_ctx->cache) return NULL;
    static int idx;
    if (state == 0) idx = 0;
    size_t len = strlen(text);
    param_cache_t *cache = g_ctx->cache;

    pthread_mutex_lock(&cache->mutex);
    while (idx < cache->count) {
        param_entry_t *e = &cache->entries[idx++];
        if (strncasecmp(e->name, text, len) == 0) {
            char *match = strdup(e->name);
            pthread_mutex_unlock(&cache->mutex);
            return match;
        }
    }
    pthread_mutex_unlock(&cache->mutex);
    return NULL;
}

static char **completion_cb(const char *text, int start, int end) {
    (void) end;
    rl_attempted_completion_over = 1; /* Kein Datei-Fallback */

    if (start == 0)
        return rl_completion_matches(text, generator_command);

    char first[32] = {0};
    sscanf(rl_line_buffer, "%31s", first);

    if (strcasecmp(first, "get") == 0 || strcasecmp(first, "set") == 0)
        return rl_completion_matches(text, generator_param);

    return NULL;
}

/* ─── Befehle ─────────────────────────────────────────────────────── */

static void cmd_help(void) {
    printf(C_BOLD C_CYAN
            "\n  MAVLink Console – Befehle\n"
            C_RESET
            "  ─────────────────────────────────────────────────────────\n"
            "  " C_CYAN "list" C_RESET " [filter]       Parameter auflisten\n"
            "                          Beispiel: list SERVO\n"
            "  " C_CYAN "get" C_RESET "  <name>         Einzelnen Parameter anzeigen\n"
            "  " C_CYAN "set" C_RESET "  <name> <wert>  Parameter setzen\n"
            "                          Beispiel: set SERVO1_MIN 1000\n"
            "  " C_CYAN "refresh" C_RESET "               PARAM_REQUEST_LIST senden\n"
            "  " C_CYAN "status" C_RESET "                Cache-Füllstand anzeigen\n"
            "  " C_CYAN "help" C_RESET "                Diese Hilfe\n"
            "  " C_CYAN "quit" C_RESET "                Programm beenden\n"
            "  ─────────────────────────────────────────────────────────\n"
            "  " C_CYAN "Tab" C_RESET "         Befehl oder Parametername vervollständigen\n"
            "  " C_CYAN "Tab Tab" C_RESET "     Alle Möglichkeiten anzeigen\n"
            "  " C_CYAN "↑ / ↓" C_RESET "      Eingabe-Historie\n\n"
            );
}

static const char *param_type_name(uint8_t type) {
    switch (type) {
        case 1:  return "uint8";
        case 2:  return "int8";
        case 3:  return "uint16";
        case 4:  return "int16";
        case 5:  return "uint32";
        case 6:  return "int32";
        case 9:  return "REAL32";
        case 10: return "REAL64";
        default: return "?";
    }
}

static void cmd_status(param_cache_t *cache) {
    int count = param_cache_get_count(cache);
    int total = param_cache_get_total(cache);
    bool complete = param_cache_is_complete(cache);

    printf(C_BOLD "\n  Parameter-Cache:\n" C_RESET);
    if (total > 0) {
        /* Fortschrittsbalken */
        const int bar_w = 32;
        int filled = (count * bar_w) / total;
        printf("  [");
        for (int i = 0; i < bar_w; i++)
            printf("%s", i < filled ? C_GREEN "█" C_RESET : "░");
        printf("]  %d / %d  %s\n\n",
                count, total,
                complete ? C_GREEN "[vollständig]" C_RESET
                : C_YELLOW "[wird empfangen…]" C_RESET);
    } else {
        printf("  %d Parameter im Cache "
                C_YELLOW "(Gesamtanzahl noch unbekannt)\n" C_RESET, count);
        printf("  → Tipp: 'refresh' senden um Parameter anzufordern.\n\n");
    }
}

static void cmd_list(param_cache_t *cache, const char *filter) {
    param_entry_t *buf = malloc(PARAM_MAX_COUNT * sizeof (param_entry_t));
    if (!buf) {
        perror("malloc");
        return;
    }

    int n = param_cache_list(cache, filter, buf, PARAM_MAX_COUNT);

    if (n == 0) {
        if (filter)
            printf(C_YELLOW "  Kein Parameter mit Filter '%s' gefunden.\n"
                C_RESET, filter);
        else
            printf(C_YELLOW "  Cache leer – 'refresh' senden.\n" C_RESET);
    } else {
        printf(C_BOLD
                "\n  %-20s  %16s  Typ  Index\n" C_RESET
                "  %-20s  %16s  ---  -----\n",
                "Parameter", "Wert",
                "--------------------", "----------------");
        for (int i = 0; i < n; i++) {
            printf("  %-20s  %16.6g  %-6s  %5d\n",
                    buf[i].name, buf[i].value, param_type_name(buf[i].type), buf[i].index);
        }
        printf(C_BOLD "\n  %d Parameter%s\n\n" C_RESET,
                n, filter ? " (gefiltert)" : " gesamt");
    }
    free(buf);
}

static void cmd_get(param_cache_t *cache, const char *name) {
    param_entry_t e;
    if (param_cache_find(cache, name, &e)) {
        printf("\n  " C_CYAN "%-20s" C_RESET " = " C_GREEN "%-16.6g" C_RESET "  Typ %-6s  Index %d\n\n",
                e.name, e.value, param_type_name(e.type), e.index);
    } else {
        printf(C_RED "\n  Parameter '%s' nicht im Cache.\n" C_RESET, name);
        printf("  → 'refresh' senden, dann erneut versuchen.\n\n");
    }
}

static void cmd_set(proxy_ctx_t *ctx, const char *name, const char *val_str) {
    char *endptr;
    float value = strtof(val_str, &endptr);
    if (*endptr != '\0') {
        printf(C_RED "\n  Ungültiger Wert '%s' – Dezimalzahl erwartet.\n\n"
                C_RESET, val_str);
        return;
    }

    /* Typ aus Cache, Fallback: REAL32 */
    param_entry_t e;
    uint8_t type = MAV_PARAM_TYPE_REAL32;
    if (param_cache_find(ctx->cache, name, &e))
        type = e.type;

    printf("\n  Setze %-20s = %.6g … ", name, value);
    fflush(stdout);

    if (!mav_send_param_set(ctx->serial_fd, name, value, type)) {
        printf(C_RED "Sendefehler!\n\n" C_RESET);
        return;
    }

    /* Auf Bestätigung warten – max. 3 Sekunden */
    for (int i = 0; i < 30; i++) {
        usleep(100000); /* 100 ms */
        param_entry_t after;
        if (param_cache_find(ctx->cache, name, &after)) {
            if (fabsf(after.value - value) < 1e-5f) {
                printf(C_GREEN "OK  (FC bestätigt: %.6g)\n\n" C_RESET,
                        after.value);
                return;
            }
        }
    }
    printf(C_YELLOW "Keine Bestätigung vom FC (Timeout 3 s).\n\n" C_RESET);
}

/* ─── Haupt-Konsolen-Loop ─────────────────────────────────────────── */

void console_run(proxy_ctx_t *ctx) {
    g_ctx = ctx;

    /* readline konfigurieren */
    rl_attempted_completion_function = completion_cb;
    rl_completer_quote_characters = "\"'";
    rl_bind_key('\t', rl_complete);

    /* Geschichte laden */
    using_history();
    read_history(HISTORY_FILE);

    /* Banner */
    printf(C_BOLD C_CYAN
            "\n"
            "  ┌──────────────────────────────────────────────────┐\n"
            "  │     MAVLink Console  –  Interaktive Konsole      │\n"
            "  └──────────────────────────────────────────────────┘\n"
            C_RESET
            "  " C_CYAN "help" C_RESET " → Befehle    "
            C_CYAN "Tab" C_RESET " → Vervollständigen    "
            C_CYAN "↑/↓" C_RESET " → Historie\n\n");

    const char *prompt = C_BOLD "mavconsole> " C_RESET;
    char *line;

    /* Schleife läuft bis: quit-Befehl, EOF (Ctrl+D) oder Stop-Signal */
    while (!*ctx->stop && (line = readline(prompt)) != NULL) {

        /* Leerzeilen überspringen */
        if (!*line) {
            free(line);
            continue;
        }

        add_history(line);

        /* Tokenisieren */
        char *argv_tbl[8];
        int argc = 0;
        char *save;
        char *tok = strtok_r(line, " \t", &save);
        while (tok && argc < 8) {
            argv_tbl[argc++] = tok;
            tok = strtok_r(NULL, " \t", &save);
        }

        if (argc == 0) {
            free(line);
            continue;
        }

        const char *cmd = argv_tbl[0];

        if (strcasecmp(cmd, "help") == 0) {
            cmd_help();

        } else if (strcasecmp(cmd, "status") == 0) {
            cmd_status(ctx->cache);

        } else if (strcasecmp(cmd, "list") == 0) {
            cmd_list(ctx->cache, argc > 1 ? argv_tbl[1] : NULL);

        } else if (strcasecmp(cmd, "get") == 0) {
            if (argc < 2)
                printf(C_RED "  Verwendung: get <parametername>\n\n" C_RESET);
            else
                cmd_get(ctx->cache, argv_tbl[1]);

        } else if (strcasecmp(cmd, "set") == 0) {
            if (argc < 3)
                printf(C_RED "  Verwendung: set <parametername> <wert>\n\n"
                    C_RESET);
            else
                cmd_set(ctx, argv_tbl[1], argv_tbl[2]);

        } else if (strcasecmp(cmd, "refresh") == 0) {
            mav_send_param_request_list(ctx->serial_fd);
            printf(C_GREEN "\n  PARAM_REQUEST_LIST gesendet.\n" C_RESET);
            printf("  Parameter kommen asynchron an.\n");
            printf("  'status' → Fortschritt,  'list' → Ergebnisse\n\n");

        } else if (strcasecmp(cmd, "quit") == 0 ||
                strcasecmp(cmd, "exit") == 0) {
            printf(C_YELLOW "\n  Programm wird beendet…\n\n" C_RESET);
            *ctx->stop = 1;
            free(line);
            break;

        } else {
            printf(C_RED "\n  Unbekannter Befehl: '%s'\n" C_RESET, cmd);
            printf("  'help' für eine Übersicht.\n\n");
        }

        free(line);
    }

    write_history(HISTORY_FILE);
    clear_history();
}
