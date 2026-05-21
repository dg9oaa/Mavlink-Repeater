/*
 * MIT License
 * Copyright (c) 2025 Jonny Röker
 *
 * File: param_cache.h
 * Thread-sicherer Zwischenspeicher für MAVLink-Parameter (PARAM_VALUE).
 * Der Serial-RX-Thread befüllt den Cache; die Konsole liest/schreibt ihn.
 */

#ifndef PARAM_CACHE_H
#define PARAM_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* MAVLink erlaubt maximal 16 Zeichen + '\0' */
#define PARAM_NAME_LEN   17
/* ArduPilot hat typisch 500–800 Parameter */
#define PARAM_MAX_COUNT  1500

typedef struct {
    char     name[PARAM_NAME_LEN];  /* Parametername (null-terminiert)   */
    float    value;                 /* Aktueller Wert                    */
    uint8_t  type;                  /* MAV_PARAM_TYPE                    */
    uint16_t index;                 /* Index laut FC                     */
    bool     valid;                 /* Eintrag gültig?                   */
} param_entry_t;

typedef struct {
    param_entry_t   entries[PARAM_MAX_COUNT];
    int             count;          /* Bisher empfangene Parameter       */
    int             total;          /* Vom FC gemeldete Gesamtanzahl     */
    bool            complete;       /* Alle Parameter vollständig?       */
    pthread_mutex_t mutex;
} param_cache_t;

/* Lifecycle */
void  param_cache_init   (param_cache_t *cache);
void  param_cache_destroy(param_cache_t *cache);

/* Schreiben (aus Serial-RX-Thread) */
void  param_cache_update (param_cache_t *cache,
                          const char *name, float value,
                          uint8_t type, uint16_t index, uint16_t total);

/* Lesen (aus Konsolen-Thread / Main-Thread) */
bool  param_cache_find   (param_cache_t *cache,
                          const char *name, param_entry_t *out);
int   param_cache_list   (param_cache_t *cache,
                          const char *filter,
                          param_entry_t *out, int max_out);

/* Status-Abfragen */
int   param_cache_get_count   (param_cache_t *cache);
int   param_cache_get_total   (param_cache_t *cache);
bool  param_cache_is_complete (param_cache_t *cache);

#endif /* PARAM_CACHE_H */
