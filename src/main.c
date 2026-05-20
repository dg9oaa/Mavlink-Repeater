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

#include "serial.h"
#include "udp.h"
#include "option.h"
#include "common/mavlink.h"
#include "logging.h"
#include "console.h"

#define JSON_CONFIG_FILE "/etc/mavlink-repeater.json"
#define PID_FILE "/tmp/mavrpts.pid"

#define SERIAL_DEVICE "/dev/ttyACM0"
#define SERIAL_DEVICE_BAUDRATE 57600

#define UDP_IP "10.100.1.102"
#define UDP_PORT 14550

extern char *progname;
options_t options;
jsonconfig_t jsonconfig;
prognames_t prognames;

void write_pidfile(const char* filename);

volatile sig_atomic_t stop_requested = 0;
volatile sig_atomic_t reconfigure = 0;

/**
 * initialize
 */
void initialize() {
    LogLevel initialize_loglevel = LOGLEVEL_WARN;
    log_set_level(initialize_loglevel);
    char tmpll[10];
    sprintf(tmpll, "%s", loglevel_to_string(initialize_loglevel));
    strcpy(options.loglevel_dflt, tmpll);
    strcpy(options.loglevel, options.loglevel_dflt);
    //log_set_file("/tmp/mavtest.log", 512 * 1024);

    // for test
/*
    strcpy(options.loglevel, "debug");
    log_set_level(loglevel_from_string(options.loglevel));
    log_set_level(LOGLEVEL_DEBUG);
*/

    jsonconfig.defaultfile = JSON_CONFIG_FILE;
    jsonconfig.is_used = true;
    prognames.mav_repeater = "mavrpt";    // MAVLink repeater forwards directly from the air station to the ground station
    prognames.mav_repeater_client = "mavrptclient";    // MAVLink repeater Client routes the data from the Air Station through the existing IP tunnel to the Ground Station server
    prognames.mav_repeater_server = "mavrptserver";    // MAVLink repeater Server handles the connection from the IP tunnel of the air station/clients and establishes a connection to a ground station software
    prognames.mav_console = "mavconsole";

    strncpy(options.device_dflt, SERIAL_DEVICE, sizeof options.device_dflt);
    options.baudrate_dflt = SERIAL_DEVICE_BAUDRATE;
}


void signal_handler(int signal) {
    if (signal == SIGHUP) {
        reconfigure = 1;
    }
    if (signal == SIGINT || signal == SIGTERM) {
        stop_requested = 1;
        // Optional: sofortige Aufräumaktion hier
        // z.B. printf("Signal %d empfangen\n", signal);
    }
}

int main(int argc, char *argv[]) {

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);

    get_program_name(argv);
    initialize();

    // first test json config exists
    if (parse_config(argc, argv)) {
        printf("Konfiguration für '%s':\n", jsonconfig.function);
        printf("  device   : %s\n", options.device);
        printf("  baudrate : %d\n", options.baudrate);
        printf("  server   : %s\n", options.server);
        printf("  loglevel : %s\n", options.loglevel);
        printf("  daemon   : %d\n", options.daemon);
        printf("  logfile  : %s\n", options.logfile);
        printf("  logfilesize: %d\n", options.logfilesize);
  //      exit(EXIT_SUCCESS);

        // set logfile and size
        if (strlen(options.logfile) > 0) {
            int logsize = 0;
            if (options.logfilesize > 0)
                logsize = options.logfilesize;
            else
                logsize = LOGFILE_DEFAULT_SIZE;
            log_set_file(options.logfile, logsize);
        } else {
            logSTD(); // log to stdout/stderror
        }

        // set loglevel
        if (strcmp(options.loglevel, options.loglevel_dflt) != 0) {
            log_set_level(loglevel_from_string(options.loglevel));
        }


    } else {
        options.baudrate = options.baudrate_dflt;
        strcpy(options.device, options.device_dflt);
    }

    parse_options(argc, argv);

    //LOG__DEBUG("Teste Logging %s", options.loglevel);
    //LOG__INFO("Teste Logging %s", options.loglevel);
    //LOG__WARN("Teste Logging %s", options.loglevel);
    //LOG__ERROR("Teste Logging %s", options.loglevel);

    
    if (options.daemon) {
        pid_t pid = fork();
        switch (pid) {
            case -1: /* Fehler */
                fprintf(stderr, "%s: error to fork(): %s.\n", progname, strerror(errno));
                exit(EXIT_FAILURE);
                break;
            case 0: /* Child process continues */
                break;
            default: /* Parent process terminated immediately */
                exit(EXIT_SUCCESS);
                break;
        }

        if (setsid() < 0) {
            fprintf(stderr, "%s: error in setsid(): %s\n", progname, strerror(errno));
            exit(EXIT_FAILURE);
        }

        //       switch (pid = fork()) {
        //           case -1: /* Fehler */
        //               syslog(LOG_ERR, "Fehler in fork(): %s.\n", strerror(errno));
        //               exit(EXIT_FAILURE);
        //               break;
        //           case 0: /* Child process continues */
        //               break;
        //           default: /* Parent process terminated immediately */
        //               exit(EXIT_SUCCESS);
        //               break;
        //       }
        //        close(STDIN_FILENO);
        //        close(STDOUT_FILENO);
        //        close(STDERR_FILENO);

        //        chdir("/");
        //        umask(0);
    } else {
        LOG__DEBUG("process continues direct, no daemon");
    }

    // ab hier test
    
    if (options.daemon) sleep(10);

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
    uint8_t buffer[1024];
    mavlink_message_t msg;
    mavlink_status_t status;

    while (! stop_requested) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serial_fd, &readfds);
        FD_SET(udp_fd, &readfds);

        int maxfd = (serial_fd > udp_fd) ? serial_fd : udp_fd;
        struct timeval timeout = {1, 0};

        int result = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (result < 0) {
            if (errno == EINTR && stop_requested) {
                LOG__INFO("Program termination detected");
                break;

            } else if (errno == EINTR && reconfigure) {
                //printf("Signalunterbrechung erkannt. Reconfigure....\n");
                // implement later
                continue;
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
            LOG__TRACE("read %d bytes from serial port...", len);
            if (len > 0) {
                send_udp_packet(udp_fd, buffer, len);
            }
        }

        LOG__TRACE("try to read udp port...");
        if (FD_ISSET(udp_fd, &readfds)) {
            ssize_t len = recv_udp_packet(udp_fd, buffer, sizeof(buffer));
            LOG__TRACE("read %d bytes from udp port...", len);
            if (len > 0) {
                writeSerial(serial_fd, buffer, len);
            }
        }
    }

    while (false) {
        int len = readSerial(serial_fd, buffer, sizeof (buffer));
        if (len <= 0) {
            usleep(1000);
            continue;
        }
        //printf("begin len%d\n", len);
        for (int i = 0; i < len; ++i) {
            if (mavlink_parse_char(MAVLINK_COMM_0, buffer[i], &msg, &status)) {

                if (msg.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                    mavlink_heartbeat_t hb;
                    mavlink_msg_heartbeat_decode(&msg, &hb);
                    bool armed = hb.base_mode & MAV_MODE_FLAG_SAFETY_ARMED;
                    printf("ARMED STATUS: %s\n", armed ? "YES" : "NO");

                } else if (msg.msgid == MAVLINK_MSG_ID_SYS_STATUS) {
                    mavlink_sys_status_t sys;
                    mavlink_msg_sys_status_decode(&msg, &sys);

                    float voltage = sys.voltage_battery / 1000.0f; // mV → V
                    float current = sys.current_battery / 100.0f; // cA → A

                    printf("Batterie: %.2f V, %.2f A\n", voltage, current);

                } else if (msg.msgid == MAVLINK_MSG_ID_BATTERY_STATUS) { /* 147 */
                    mavlink_battery_status_t bat;
                    mavlink_msg_battery_status_decode(&msg, &bat);

                    /*printf("Battery ID: %d\n", bat.id);
                    for (int i = 0; i < bat.cell_count; ++i) {
                        if (bat.voltages[i] != UINT16_MAX)
                            printf("        Zelle %d: %.3f V\n", i + 1, bat.voltages[i] / 1000.0f);
                    }
                    printf("  Strom: %.2f A\n", bat.current_battery / 100.0f);*/

                } else if (msg.msgid == MAVLINK_MSG_ID_POWER_STATUS            /* 125 */ ||
                        msg.msgid == MAVLINK_MSG_ID_SERVO_OUTPUT_RAW           /*  36 */ ||
                        msg.msgid == MAVLINK_MSG_ID_SYSTEM_TIME                /*   2 */ ||
                        msg.msgid == MAVLINK_MSG_ID_NAV_CONTROLLER_OUTPUT      /* 62 */ ||
                        msg.msgid == MAVLINK_MSG_ID_RC_CHANNELS                /* 65 */ ||
                        msg.msgid == MAVLINK_MSG_ID_RAW_IMU                    /* 27 */ ||
                        msg.msgid == MAVLINK_MSG_ID_GPS_RAW_INT                /* 24 */ ||
                        msg.msgid == MAVLINK_MSG_ID_SCALED_PRESSURE            /* 29 */ ||
                        msg.msgid == MAVLINK_MSG_ID_ATTITUDE                   /* 30 */ ||
                        msg.msgid == MAVLINK_MSG_ID_LOCAL_POSITION_NED         /* 32 */ ||
                        msg.msgid == MAVLINK_MSG_ID_GLOBAL_POSITION_INT        /* 33 */ ||
                        msg.msgid == MAVLINK_MSG_ID_MISSION_CURRENT            /* 42*/ ||
                        msg.msgid == MAVLINK_MSG_ID_VFR_HUD                    /* 74 */ ||
                        msg.msgid == MAVLINK_MSG_ID_POSITION_TARGET_GLOBAL_INT /* 87 */ ||
                        msg.msgid == MAVLINK_MSG_ID_TIMESYNC                   /* 111 */ ||
                        msg.msgid == MAVLINK_MSG_ID_TERRAIN_REQUEST            /* 133 */ ||
                        msg.msgid == MAVLINK_MSG_ID_TERRAIN_CHECK              /* 135 */ ||
                        msg.msgid == MAVLINK_MSG_ID_TERRAIN_REPORT             /* 136 */ ||
                        msg.msgid == MAVLINK_MSG_ID_VIBRATION                  /* 241 */
                        ) {

                    //printf("### finde diese msg-id %d\n", msg.msgid);

                } else {
                    printf("### unknown msg-id %d\n", msg.msgid);
                }

                uint8_t out_buf[MAVLINK_MAX_PACKET_LEN];
                int out_len = mavlink_msg_to_send_buffer(out_buf, &msg);
                send_udp_packet(udp_fd, out_buf, out_len);
            }
        }
        //printf("ende\n");

        uint8_t bufferRX[1024];
        int lenRX = recv_udp_packet(udp_fd, bufferRX, sizeof(bufferRX));
        //printf("empfange per udp %d bytes\n", lenRX);
        if (lenRX <= 0) {
            usleep(1000);
            continue;
        }
        for (int i = 0; i < lenRX; ++i) {
            mavlink_message_t msgRX;
            mavlink_status_t statusRX;
            if (mavlink_parse_char(MAVLINK_COMM_1, bufferRX[i], &msgRX, &statusRX)) {
                printf("[UDP] MAVLink msgid: %d\n", msgRX.msgid);
                // → Hier MAVLink von Mission Planner verarbeiten
                uint8_t out_buf[MAVLINK_MAX_PACKET_LEN];
                int out_len = mavlink_msg_to_send_buffer(out_buf, &msgRX);
                printf("upd-rx-> len:%d\n", out_len);
                writeSerial(serial_fd, out_buf, out_len);
            }
        }
        //writeSerial(serial_fd, bufferRX, sizeof(bufferRX));
        
    }

    LOG__INFO("Program will be terminated");
    closeSerial(serial_fd);
    close(udp_fd);
    unlink(PID_FILE);
    return 0;
}

/**
 * write the process-id in a file
 * @param filename
 */
void write_pidfile(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("Can't write PID-File");
        return;
    }

    fprintf(f, "%d\n", getpid());
    fclose(f);
}
