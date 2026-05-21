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
 * File:   option.c
 * Author: jonny
 *
 * Created on 29. Mai 2025
 */


#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#include "option.h"
#include "logging.h"

#include "cJSON.h"

extern options_t options;
extern jsonconfig_t jsonconfig;
extern prognames_t prognames;
char *progname;
int  shift = 1;


#define FILESEPERATOR '/'


static struct option cmd_options[] = {
    {"help",      no_argument,       0, 'h'},
    {"daemon",    no_argument,       0, 'D'},
    {"loglevel",  required_argument, 0, 'L'},
    {"console",   no_argument,       0, 'T'},

    {"device",    required_argument, 0, 'd'},
    {"baudrate",  required_argument, 0, 'b'},

    {"server",    required_argument, 0, 's'},
    {"port",      required_argument, 0, 'p'},

    {"config",    required_argument, 0, 'c'},
    {"noconfig",  no_argument,       0, 'n'},
    {"function",  required_argument, 0, 'f'},
    {0, 0, 0, 0}
};


void parse_options(int argc, char *argv[]) {
    if (argc > 0) {
        char buf[512] = {0};
        int pos = 0;
        for (int i = 1; i < argc && pos < (int) sizeof (buf) - 1; i++) {
            pos += snprintf(buf + pos, sizeof (buf) - pos,
                    "%s%s", i > 1 ? " " : "", argv[i]);
        }
        LOG__DEBUG("parse_options: %s", buf);
    }

    int opt = 0;
    int long_index = 0;
    optind = 1;

    while ((opt = getopt_long_only(argc, argv, "", cmd_options, &long_index)) != -1) {
        //printf("opt2 %c %d\n", opt, shift);
        switch (opt) {
            case 'h':
                print_usage();
                break;

            case 'D':
                options.daemon = true;
                printf("run as daemon\n");
                break;

            case 'L':
            {
                char *str = strdup(optarg);
                char *s = str;
                while (*s) {
                    *s = toupper((unsigned char) *s);
                    s++;
                }
                log_set_level(loglevel_from_string(str));
                strncpy(options.loglevel, str, sizeof options.loglevel);
                log_set_level(loglevel_from_string(options.loglevel));
            }
                break;

            case 'd':
                strncpy(options.device, optarg, sizeof options.device);
                break;

            case 'b':
                options.baudrate = atoi(optarg);
                break;

            case 'p':
                options.port = atoi(optarg);
                break;

            case 'T':
                options.console = true;
                break;

            case 'c':
            case 'n':
            case 'f':
                // do nothing
                break;

            default:
                print_usage();
                break;
        }
    }
    if (options.baudrate == 0)
        options.baudrate = options.baudrate_dflt;
    if (strlen(options.device) == 0)
        strcpy(options.device, options.device_dflt);

}

/**
 * check json config
 * 
 * @param argc
 * @param argv
 * @return true if found valid json config
 */
bool parse_config(int argc, char *argv[]) {
    int opt = 0;
    int json_index = 0;

    while ((opt = getopt_long_only(argc, argv, "", cmd_options, &json_index)) != -1) {
        //printf("opt1 %c\n", opt);
        switch (opt) {
            case 'h':
                print_usage();
                break;

            case 'c':
                jsonconfig.file = optarg;
                shift++;
                break;

            case 'n':
                jsonconfig.file = NULL;
                jsonconfig.is_used = false;
                shift++;
                break;
                
            case 'f':
                jsonconfig.function = optarg;
                shift++;
                break;
        }
    }

    if (jsonconfig.file == NULL && jsonconfig.is_used) {
        jsonconfig.file = jsonconfig.defaultfile;
    }

    LOG__DEBUG("default json file:%s   file given:%s   function:%s   noconfig:%d\n", jsonconfig.defaultfile, jsonconfig.file, jsonconfig.function, jsonconfig.is_used);

    if (jsonconfig.function == NULL && jsonconfig.is_used) {
        if ((strcmp(prognames.mav_repeater, progname) == 0) || (strcmp(prognames.mav_repeater_client, progname) == 0) ||
                (strcmp(prognames.mav_repeater_server, progname) == 0) || (strcmp(prognames.mav_console, progname) == 0)) {
            LOG__DEBUG("search %s in json config", progname);
            jsonconfig.function = progname;
        } else {
            LOG__WARN("No configuration for %s found", progname);
            return false;
        }
    }

    if (jsonconfig.is_used == false) {
        LOG__DEBUG("No configuration file is used");
    } else if (load_config_from_json(jsonconfig.file, &options, jsonconfig.function) == 0) {
        if (strlen(options.logfile) == 0) {
            fprintf(stderr, "%s: No log file configured\n", progname);
        }
/*
        printf("Konfiguration für '%s':\n", jsonconfig.function);
        printf("  device   : %s\n", options.device);
        printf("  baudrate : %d\n", options.baudrate);
        printf("  loglevel : %s\n", options.loglevel);
        printf("  daemon   : %d\n", options.daemon);
        printf("  logfile  : %s\n", options.logfile);
        printf("  logfilesize: %d\n", options.logfilesize);
*/
    } else {
        LOG__WARN("Could not load configuration");
        return false;
    }

    return true;
}

/**
 * parse the json file
 * 
 * @param filename
 * @param the options
 * @return 0 if true
 */
int load_config_from_json(const char* filename, options_t* cfg, const char* function) {

    FILE* jsonfile = fopen(filename, "rb");
    if (!jsonfile) {
        LOG__ERROR("%s: %s - No such file or directory", progname, filename);
        return -1;
    }

    fseek(jsonfile, 0, SEEK_END);
    long len = ftell(jsonfile);
    fseek(jsonfile, 0, SEEK_SET);

    char* data = malloc(len + 1);
    if (!data) {
        fclose(jsonfile);
        LOG__ERROR("%s: memory allocation failed", progname);
        return -1;
    }

    fread(data, 1, len, jsonfile);
    data[len] = 0;
    fclose(jsonfile);
    
    cJSON* root = cJSON_Parse(data);
    free(data);

    if (!root) {
        LOG__ERROR("%s: error to parse JSON-File", progname);
        return -1;
    }

    // Zuerst globalen Loglevel lesen
    cJSON* global = cJSON_GetObjectItemCaseSensitive(root, "global");
    if (global) {
        cJSON* logitem = cJSON_GetObjectItemCaseSensitive(global, "loglevel");
        if (cJSON_IsString(logitem) && logitem->valuestring) {
            strncpy(cfg->loglevel, logitem->valuestring, sizeof (cfg->loglevel) - 1);
        }
        logitem = cJSON_GetObjectItemCaseSensitive(global, "logfile");
        if (cJSON_IsString(logitem) && logitem->valuestring) {
            strncpy(cfg->logfile, logitem->valuestring, sizeof (cfg->logfile) - 1);
        }
        logitem = cJSON_GetObjectItemCaseSensitive(global, "logfilesize");
        if (cJSON_IsNumber(logitem)) {
            cfg->logfilesize = logitem->valueint;
        }
        
    }

    cJSON* section = cJSON_GetObjectItemCaseSensitive(root, function);
    if (!section) {
        LOG__WARN("mode '%s' not found", function);
        cJSON_Delete(root);
        return 1;
    }

    cJSON* item;

    item = cJSON_GetObjectItemCaseSensitive(section, "device");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(cfg->device, item->valuestring, sizeof (cfg->device) - 1);
    }

    item = cJSON_GetObjectItemCaseSensitive(section, "baudrate");
    if (cJSON_IsNumber(item)) {
        cfg->baudrate = item->valueint;
    }

    item = cJSON_GetObjectItemCaseSensitive(section, "server");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(cfg->server, item->valuestring, sizeof (cfg->server) - 1);
    }

    item = cJSON_GetObjectItemCaseSensitive(section, "port");
    if (cJSON_IsNumber(item)) {
        cfg->port = item->valueint;
    }

    item = cJSON_GetObjectItemCaseSensitive(section, "loglevel");
    if (cJSON_IsString(item) && item->valuestring) {
        // local loglevel overwrite
        strncpy(cfg->loglevel, item->valuestring, sizeof (cfg->loglevel) - 1);
    }

    item = cJSON_GetObjectItemCaseSensitive(section, "daemon");
    if (cJSON_IsBool(item)) {
        cfg->daemon = cJSON_IsTrue(item);
    }

    cJSON_Delete(root);

    return 0;
}

/**
 * get the program name
 * @param argv
 */
void get_program_name(char *argv[]) {
    char *prognamep;
    
    if ((prognamep = strrchr(argv[0], FILESEPERATOR)) == NULL)
        progname = argv[0];
    else {
        ++prognamep;
        progname = prognamep;
    }
}

void print_usage() {
    printf(
            "\n"
            "Usage: %s [OPTIONS]\n"
            "\n"
            "  --device      Serial MAVLink device (%s by default)\n"
            "  --baudrate    Serial MAVLink baudrate (%d by default)\n"
            "  --server      Server address (%s by default)\n"
            "  --port        Server port (%d by default)\n"
            "  --console     Use MAVLink Console\n"
            "  --loglevel    Setting the log level (%s by default)\n"
            "  --daemon      Runs the program in the background and detaches it from the input shell\n"
            "  --function    Program function (client, server, direct or console). Is actually controlled via the program name (mavrptclient, mavrptserver, mavrpt or mavconsole)\n"
            "  --help        Display this help\n"
            , progname, options.device_dflt, options.baudrate_dflt, options.server, options.port, options.loglevel_dflt );

    printf(
            "\n or\n"
            "  --config <json config file>  (%s by default)\n"
            "\n", jsonconfig.defaultfile);

    printf(
            "\n"
            "\n  Program name or function name:"
            "  mavrptclient - MAVLink repeater Client routes the data from the Air Station through the existing IP tunnel to the Ground Station server.\n"
            "  mavrptserver - MAVLink repeater Server handles the connection from the IP tunnel of the air station/clients and establishes a connection to a ground station software.\n"
            "  mavrpt       - MAVLink repeater forwards directly from the air station to the ground station. Similar to MAVProxy.\n"
            "  mavconsole   - MAVLink console  allows to read and change configuration parameters\n");
    
    exit(EXIT_FAILURE);
}
