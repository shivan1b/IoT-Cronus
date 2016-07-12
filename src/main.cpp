//
//  main.cpp
//  
//
//  Created by Ilya Albekht on 11/29/15.
//
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>

#include "cal.h"
#include "net.h"
#include "valve.h"
#include "gpio.h"

#include "log.h"

#include <signal.h>

#include "ArduinoJson/ArduinoJson.hpp"


/*
#include <libical/ical.h>

#include "libical/icalproperty_cxx.h"
#include "libical/vcomponent_cxx.h"
#include "libical/icalrecur.h" */


// Time after which to check if any of the valves are active (sec)
#define CHECK_TIME 10
// Time to update calendar info from the internet (sec)
#define UPDATE_TIME (60*60*10)

//// TODO List
// * Extract cal for a week

using namespace std;
using namespace LibICal;

// Type declaration
struct valve_t
{
    iCalValveControl vc;
    GpioRelay gpio;
    const string url;

    valve_t(string ical_url, int gpio_pin) : gpio(gpio_pin), vc(GT_DEFUALT), url(ical_url) {}
};

// Global variables
int g_DebugLevel=6;
std::vector<shared_ptr<valve_t>> valves;

// Signal handlers
void sighandler_stop(int id)
{
    for(auto &v: valves)
        v.reset();
    exit(0);
}

#define PIN_V1 7
#define PIN_V2 8
#define PIN_LED_ACTIVE 4

void ParseOptions(int argc, char* argv[])
{
    int i=1;
    for(; i<argc; i++)
    {
        if(strcmp("--debug", argv[i]) == 0)
        {
            g_DebugLevel = 7;
            DEBUG_PRINT(LOG_INFO, "Debug print enabled");
        }

        // Run channel <CH> for <N> minutes
        // must be last one in the list
        if(strcmp("--run", argv[i]) == 0)
        {
            if( argc < i+2 )
            {
                cerr << "Invalid number of arguments\n";
                exit(-1);
            }

            int ch = atoi( argv[i+1] );
            int t  = atoi( argv[i+2] );

            DEBUG_PRINT(LOG_INFO, "Start channel " << ch << " for " << t << " minutes" );

            GpioRelay R(ch);

            R.Start();
            sleep(t*60);
            R.Stop();

            exit(0);
        }
    }
}

int main(int argc, char* argv[])
{

    ParseOptions(argc, argv);

    // Register signals
    signal(SIGINT,  sighandler_stop);
    signal(SIGKILL, sighandler_stop);
    signal(SIGTERM, sighandler_stop);


    // Start main app loop
    // Should go to deamon mode at this point

    //iCalValveControl vc(GT_DEFUALT);
    //GpioRelay R1(PIN_V1);

    {
        ifstream cf("config.json");
        string config_str((std::istreambuf_iterator<char>(cf)),
                         std::istreambuf_iterator<char>());
        cf.close();

        StaticJsonBuffer<512> jbuf;
        JsonObject& root = jbuf.parseObject(config_str);

        if(!root.success())
        {
            DEBUG_PRINT(LOG_WARNING, "Error parsing config.json");
            exit(-1);
        }
        
        int n_s = root["schedule"].size();
        cout << "Num sch " << n_s << endl;
        for(int i=0; i<n_s; ++i)
        {
            cout << "add sch\n";
            cout << root["schedule"][i]["type"] << endl;
            if(root["schedule"][i]["type"] == string("ical"))
            {
                cout << "sch == ical";
                // Adding ical type schedule
                cout << root["schedule"][i]["url"] << endl;
                string gpio_id = root["schedule"][i]["igpo"][0];
                shared_ptr<valve_t> vt(new valve_t(root["schedule"][i]["url"],std::stoi(gpio_id)) );

                char f_name[255];
                sprintf(f_name, "%d.ics", vt->gpio.GetPin());
                vt->vc.ParseICALFromFile(f_name);
                valves.push_back(vt);
            }
        }
    }

    /*{
        shared_ptr<valve_t> vt(new valve_t("https://calendar.google.com/calendar/ical/04n0submlvfodumeo7ola6f90s%40group.calendar.google.com/private-b40357d4bfaee14d76ffaa65e910d554/basic.ics",
            PIN_V1) );
        char f_name[255];
        sprintf(f_name, "%d.ics", vt->gpio.GetPin());
        vt->vc.ParseICALFromFile(f_name);

        valves.push_back(vt);
    }*/

    // vc.ParseICALFromFile("./7.ics");
    time_t last_reload = time(0); //-2*UPDATE_TIME; // Hack to force the reload on the first iteration

    // Enter into work loop
    while(true)
    {
        for( auto &vt : valves)
        {
            if( (time(0) - last_reload) > UPDATE_TIME )    // do it every UPDATE_TIME seconds
            {
                string ical_string("");
                // string URL("https://calendar.google.com/calendar/ical/04n0submlvfodumeo7ola6f90s%40group.calendar.google.com/private-b40357d4bfaee14d76ffaa65e910d554/basic.ics");
                int err;

                if( (err = load_ical_from_url(ical_string, vt->url, vt->vc.LastUpdated())) == NET_SUCCESS)
                {
                    vt->vc.ParseICALFromString(ical_string);

                    // TODO - store to $ file
                }
                else if(err == NET_NOT_CHANGED)
                {
                    //TODO force reload from the cache once a day to generate schedule for 7 days
                    //      and store it in the cache
                }
                else if(err < 0)
                {
                    DEBUG_PRINT(LOG_INFO, "ERROR: Error loading ical" );
                }

                // FIXME ?!?
                last_reload = time(0); // is it better to update the time before or after the load??
            }

            DEBUG_PRINT(LOG_INFO, "INFO: update status");
            //set_valve_status(0, vc.IsActive());
            if(vt->vc.IsActive() != vt->gpio.GetStatus())
                vt->gpio.SetStatus(vt->vc.IsActive());
        }

        // TODO adjust sleep time for schedule update
        sleep(CHECK_TIME);
    }
    
    return 0;
}

