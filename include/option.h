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
 * File:   option.h
 * Author: jonny
 *
 * Created on 31. Mai 2025
 */

#ifndef OPTION_H
#define OPTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

    typedef struct __options_t {
        bool daemon;
        char loglevel[16];
        char loglevel_dflt[16];
        char logfile[256];
        int  logfilesize;
        char device[32];
        char device_dflt[32];
        int  baudrate;
        int  baudrate_dflt;
        char server[32];
        int  port;
        char function[32];
        bool console;
    } options_t;

    typedef struct __jsonconfig_t {
        bool is_used;
        char *file;
        char *defaultfile;
        char *function;
    } jsonconfig_t;

    typedef struct __prognames_t {
        char* mav_repeater_client;
        char* mav_repeater_server;
        char* mav_repeater;
        char* mav_console;
    } prognames_t;

    typedef enum {
        mavrptclient,
        mavrptserver,
        mavrpt
    } ProgFunction;

    void parse_options(int argc, char *argv[]);
    bool parse_config(int argc, char *argv[]);
    void print_usage();
    void get_program_name(char *argv[]);
    int  load_config_from_json(const char* filename, options_t* cfg, const char* function);

#ifdef __cplusplus
}
#endif

#endif /* OPTION_H */