/*
 * MIT License
 * Copyright (c) 2025 Jonny Röker
 *
 * File: param_cache.c
 */

#define _GNU_SOURCE   /* strcasestr */

#include "param_cache.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────── */

void param_cache_init(param_cache_t *cache) {
    memset(cache, 0, sizeof(*cache));
    pthread_mutex_init(&cache->mutex, NULL);
}

void param_cache_destroy(param_cache_t *cache) {
    pthread_mutex_destroy(&cache->mutex);
}

/* ─── Schreiben ───────────────────────────────────────────────────── */

void param_cache_update(param_cache_t *cache,
                        const char *name, float value,
                        uint8_t type, uint16_t index, uint16_t total) {
    pthread_mutex_lock(&cache->mutex);

    /* Gesamtanzahl merken, sobald bekannt */
    if (total > 0 && (int)total > cache->total)
        cache->total = (int)total;

    /* Vorhandenen Eintrag suchen → aktualisieren */
    for (int i = 0; i < cache->count; i++) {
        if (strncmp(cache->entries[i].name, name, PARAM_NAME_LEN - 1) == 0) {
            cache->entries[i].value = value;
            cache->entries[i].type  = type;
            cache->entries[i].index = index;
            if (cache->total > 0 && cache->count >= cache->total)
                cache->complete = true;
            pthread_mutex_unlock(&cache->mutex);
            return;
        }
    }

    /* Neuer Eintrag */
    if (cache->count < PARAM_MAX_COUNT) {
        param_entry_t *e = &cache->entries[cache->count];
        strncpy(e->name, name, PARAM_NAME_LEN - 1);
        e->name[PARAM_NAME_LEN - 1] = '\0';
        e->value = value;
        e->type  = type;
        e->index = index;
        e->valid = true;
        cache->count++;

        if (cache->total > 0 && cache->count >= cache->total)
            cache->complete = true;
    }

    pthread_mutex_unlock(&cache->mutex);
}

/* ─── Lesen ───────────────────────────────────────────────────────── */

bool param_cache_find(param_cache_t *cache,
                      const char *name, param_entry_t *out) {
    pthread_mutex_lock(&cache->mutex);
    for (int i = 0; i < cache->count; i++) {
        if (strncmp(cache->entries[i].name, name, PARAM_NAME_LEN - 1) == 0) {
            if (out) *out = cache->entries[i];
            pthread_mutex_unlock(&cache->mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&cache->mutex);
    return false;
}

int param_cache_list(param_cache_t *cache,
                     const char *filter,
                     param_entry_t *out, int max_out) {
    pthread_mutex_lock(&cache->mutex);
    int found = 0;
    for (int i = 0; i < cache->count && found < max_out; i++) {
        if (!filter || strcasestr(cache->entries[i].name, filter))
            out[found++] = cache->entries[i];
    }
    pthread_mutex_unlock(&cache->mutex);
    return found;
}

/* ─── Status ──────────────────────────────────────────────────────── */

int param_cache_get_count(param_cache_t *cache) {
    pthread_mutex_lock(&cache->mutex);
    int c = cache->count;
    pthread_mutex_unlock(&cache->mutex);
    return c;
}

int param_cache_get_total(param_cache_t *cache) {
    pthread_mutex_lock(&cache->mutex);
    int t = cache->total;
    pthread_mutex_unlock(&cache->mutex);
    return t;
}

bool param_cache_is_complete(param_cache_t *cache) {
    pthread_mutex_lock(&cache->mutex);
    bool c = cache->complete;
    pthread_mutex_unlock(&cache->mutex);
    return c;
}