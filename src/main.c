/* 
 * MIT License
 * 
 * Copyright (c) 2025 Jonny Roeker
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * File:   main.c
 * Author: jonny
 *
 * Created on 29. Mai 2025
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/select.h>
#include <signal.h>
#include <pthread.h>          /* NEU: Threads für Console-Modus */

#include "serial.h"
#include "udp.h"
#include "option.h"
#include "common/mavlink.h"
#include "logging.h"
#include "console.h"
#include "proxy_ctx.h"        /* NEU: Gemeinsamer Laufzeit-Kontext */
#include "param_cache.h"      /* NEU: MAVLink-Parameter-Cache      */

#define JSON_CONFIG_FILE "/etc/mavlink-repeater.json"
#define PID_FILE "/tmp/mavrpts.pid"

//#define SERIAL_DEVICE "/dev/ttyACM0"
#define SERIAL_DEVICE "/dev/tty.usbmodem146103"
#define SERIAL_DEVICE_BAUDRATE 115200

#define UDP_IP "10.100.1.102"
#define UDP_PORT 14550

extern char *progname;
options_t    options;
jsonconfig_t jsonconfig;
prognames_t  prognames;

void write_pidfile(const char* filename);

volatile sig_atomic_t stop_requested = 0;
volatile sig_atomic_t reconfigure    = 0;

/* ─── initialize ──────────────────────────────────────────────────── */

void initialize() {
    LogLevel initialize_loglevel = LOGLEVEL_WARN;
    log_set_level(initialize_loglevel);
    char tmpll[10];
    sprintf(tmpll, "%s", loglevel_to_string(initialize_loglevel));
    strcpy(options.loglevel_dflt, tmpll);
    strcpy(options.loglevel, options.loglevel_dflt);

    jsonconfig.defaultfile = JSON_CONFIG_FILE;
    jsonconfig.is_used = true;
    prognames.mav_repeater        = "mavrpt";
    prognames.mav_repeater_client = "mavrptclient";
    prognames.mav_repeater_server = "mavrptserver";
    prognames.mav_console         = "mavconsole";

    strncpy(options.device_dflt, SERIAL_DEVICE, sizeof options.device_dflt);
    options.baudrate_dflt = SERIAL_DEVICE_BAUDRATE;
}

/* ─── Signal-Handler ──────────────────────────────────────────────── */

void signal_handler(int signal) {
    if (signal == SIGHUP)
        reconfigure = 1;
    if (signal == SIGINT || signal == SIGTERM)
        stop_requested = 1;
}

/* ═══════════════════════════════════════════════════════════════════
 * NEU: Thread-Funktionen für den Console-Modus
 *
 * Im normalen Proxy-Modus (mavrpt) läuft alles im Main-Thread mit
 * select(). Im Console-Modus (mavconsole) blockiert readline() den
 * Main-Thread, deshalb übernehmen zwei Hintergrund-Threads die
 * Datenweiterleitung.
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * Thread: Serial → UDP
 *
 * Liest Bytes vom seriellen Port, parst MAVLink "on the side" um
 * PARAM_VALUE-Nachrichten in den Cache zu übernehmen, und leitet
 * alle Rohdaten unmittelbar an den UDP-Socket weiter.
 */
static void *serial_rx_thread(void *arg) {
    proxy_ctx_t      *ctx    = (proxy_ctx_t *)arg;
    uint8_t           buf[1024];
    mavlink_message_t msg;
    mavlink_status_t  status;

    LOG__DEBUG("serial_rx_thread gestartet");

    while (!*ctx->stop) {
        /* select() mit Timeout für sauberes Beenden */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ctx->serial_fd, &fds);
        struct timeval tv = {0, 50000};  /* 200 ms */

        int ret = select(ctx->serial_fd + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        ssize_t len = readSerial(ctx->serial_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        /* ── Rohdaten sofort weiterleiten ── */
        send_udp_packet(ctx->udp_fd, buf, (int)len);

        /* ── MAVLink parsen: nur für den Parameter-Cache ── */
        for (ssize_t i = 0; i < len; i++) {
            if (mavlink_parse_char(MAVLINK_COMM_0, buf[i], &msg, &status)) {
                if (msg.msgid == MAVLINK_MSG_ID_PARAM_VALUE) {
                    mavlink_param_value_t pv;
                    mavlink_msg_param_value_decode(&msg, &pv);

                    /* param_id ist NICHT zwingend null-terminiert */
                    char name[PARAM_NAME_LEN] = {0};
                    strncpy(name, pv.param_id, PARAM_NAME_LEN - 1);

                    param_cache_update(ctx->cache,
                                       name,
                                       pv.param_value,
                                       pv.param_type,
                                       pv.param_index,
                                       pv.param_count);

                    LOG__DEBUG("PARAM_VALUE: %s = %g  (%d/%d)",
                               name, pv.param_value,
                               pv.param_index + 1, pv.param_count);
                }
            }
        }
    }

    LOG__DEBUG("serial_rx_thread beendet");
    return NULL;
}

/**
 * Thread: UDP → Serial
 *
 * Empfängt Pakete vom UDP-Socket (z.B. Mission Planner) und
 * schreibt sie direkt auf den seriellen Port zum Flight Controller.
 */
static void *udp_rx_thread(void *arg) {
    proxy_ctx_t *ctx = (proxy_ctx_t *)arg;
    uint8_t      buf[1024];

    LOG__DEBUG("udp_rx_thread gestartet");

    while (!*ctx->stop) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ctx->udp_fd, &fds);
        struct timeval tv = {0, 200000};  /* 200 ms */

        int ret = select(ctx->udp_fd + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        ssize_t len = recv_udp_packet(ctx->udp_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        LOG__TRACE("udp_rx_thread: %zd Bytes → Serial", len);
        writeSerial(ctx->serial_fd, buf, (int)len);
    }

    LOG__DEBUG("udp_rx_thread beendet");
    return NULL;
}

/* ─── main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP,  signal_handler);

    get_program_name(argv);
    initialize();

    /* JSON-Konfiguration lesen */
    if (parse_config(argc, argv)) {
        printf("Konfiguration für '%s':\n", jsonconfig.function);
        printf("  device     : %s\n",  options.device);
        printf("  baudrate   : %d\n",  options.baudrate);
        printf("  server     : %s\n",  options.server);
        printf("  loglevel   : %s\n",  options.loglevel);
        printf("  daemon     : %d\n",  options.daemon);
        printf("  console    : %d\n",  options.console);
        printf("  logfile    : %s\n",  options.logfile);
        printf("  logfilesize: %d\n",  options.logfilesize);

        if (strlen(options.logfile) > 0) {
            int logsize = options.logfilesize > 0
                          ? options.logfilesize
                          : LOGFILE_DEFAULT_SIZE;
            log_set_file(options.logfile, logsize);
        } else {
            logSTD();
        }

        if (strcmp(options.loglevel, options.loglevel_dflt) != 0)
            log_set_level(loglevel_from_string(options.loglevel));

    } else {
        options.baudrate = options.baudrate_dflt;
        strcpy(options.device, options.device_dflt);
    }

    parse_options(argc, argv);

    /* Daemon-Fork */
    if (options.daemon) {
        pid_t pid = fork();
        switch (pid) {
            case -1:
                fprintf(stderr, "%s: error to fork(): %s.\n", progname, strerror(errno));
                exit(EXIT_FAILURE);
            case 0:
                break;
            default:
                exit(EXIT_SUCCESS);
        }
        if (setsid() < 0) {
            fprintf(stderr, "%s: error in setsid(): %s\n",
                    progname, strerror(errno));
            exit(EXIT_FAILURE);
        }
    } else {
        LOG__DEBUG("process continues direct, no daemon");
    }

    if (options.daemon) sleep(10);

    /* Geräte öffnen */
    int serial_fd = openSerial(options.device, options.baudrate);
    if (serial_fd < 0) {
        perror("Serial open failed");
        return 1;
    }

    int udp_fd = setup_udp_client_socket(UDP_IP, UDP_PORT);
    if (udp_fd < 0) {
        perror("UDP socket failed");
        return 1;
    }

    write_pidfile(PID_FILE);

    /* ══════════════════════════════════════════════════════════════
     * Modus-Dispatch über den Programmnamen
     * ══════════════════════════════════════════════════════════════ */

    if (strcmp(progname, prognames.mav_console) == 0 || options.console ) {

        /* ── Console-Modus (mavconsole) ─────────────────────────── *
         * Zwei Hintergrund-Threads übernehmen Serial↔UDP.           *
         * Main-Thread läuft als interaktive readline-Konsole.       */

        param_cache_t cache;
        param_cache_init(&cache);

        proxy_ctx_t ctx = {
            .serial_fd = serial_fd,
            .udp_fd    = udp_fd,
            .cache     = &cache,
            .stop      = &stop_requested
        };

        pthread_t t_serial, t_udp;

        if (pthread_create(&t_serial, NULL, serial_rx_thread, &ctx) != 0) {
            perror("pthread_create serial_rx_thread");
            return 1;
        }
        if (pthread_create(&t_udp, NULL, udp_rx_thread, &ctx) != 0) {
            perror("pthread_create udp_rx_thread");
            stop_requested = 1;
            pthread_join(t_serial, NULL);
            return 1;
        }

        /* Blockiert bis "quit"-Befehl oder Ctrl+C */
        console_run(&ctx);

        /* Threads sauber beenden */
        stop_requested = 1;
        pthread_join(t_serial, NULL);
        pthread_join(t_udp,    NULL);
        param_cache_destroy(&cache);

    } else {

        /* ── Normaler Proxy-Modus (mavrpt, mavrptclient, …) ────── *
         * Unverändert: select()-Loop im Main-Thread.                */

        uint8_t buffer[1024];

        while (!stop_requested) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(serial_fd, &readfds);
            FD_SET(udp_fd,    &readfds);

            int maxfd = (serial_fd > udp_fd) ? serial_fd : udp_fd;
            struct timeval timeout = {1, 0};

            int result = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
            if (result < 0) {
                if (errno == EINTR && stop_requested) {
                    LOG__INFO("Programmbeendigung erkannt");
                    break;
                } else if (errno == EINTR && reconfigure) {
                    continue;  /* SIGHUP – später implementieren */
                } else {
                    perror("select");
                    break;
                }
            } else if (result == 0) {
                continue;
            }

            LOG__TRACE("try to read serial port...");
            if (FD_ISSET(serial_fd, &readfds)) {
                ssize_t len = readSerial(serial_fd, buffer, sizeof(buffer));
                LOG__TRACE("read %zd bytes from serial port", len);
                if (len > 0)
                    send_udp_packet(udp_fd, buffer, (int)len);
            }

            LOG__TRACE("try to read udp port...");
            if (FD_ISSET(udp_fd, &readfds)) {
                ssize_t len = recv_udp_packet(udp_fd, buffer, sizeof(buffer));
                LOG__TRACE("read %zd bytes from udp port", len);
                if (len > 0)
                    writeSerial(serial_fd, buffer, (int)len);
            }
        }
    }

    /* ─── Aufräumen ─────────────────────────────────────────────── */
    LOG__INFO("Programm wird beendet");
    closeSerial(serial_fd);
    close(udp_fd);
    unlink(PID_FILE);
    return 0;
}

/* ─── PID-Datei ───────────────────────────────────────────────────── */

void write_pidfile(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("Can't write PID-File");
        return;
    }
    fprintf(f, "%d\n", getpid());
    fclose(f);
}