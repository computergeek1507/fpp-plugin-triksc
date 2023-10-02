#include <fpp-pch.h>

#include <unistd.h>
#include <ifaddrs.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <cstring>
#include <fstream>
#include <list>
#include <vector>
#include <sstream>
#include <httpserver.hpp>
#include <cmath>
#include <mutex>
#include <regex>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>

#include <termios.h>

#include "commands/Commands.h"
#include "common.h"
#include "settings.h"
#include "Plugin.h"
#include "log.h"

#include "channeloutput/serialutil.h"

class TriksCPlugin : public FPPPlugin, public httpserver::http_resource {
public:
    int m_fd {-1};
    bool enabled {false};
  
    TriksCPlugin() : FPPPlugin("fpp-plugin-triksc") {
        LogInfo(VB_PLUGIN, "Initializing TriksC Plugin\n");        
        enabled = InitSerial();
    }
    virtual ~TriksCPlugin() {   
        CloseSerial();     
    }
    bool InitSerial() {
        if (FileExists(FPP_DIR_CONFIG("/plugin.triksc.json"))) {
            std::string port;
            std::string panels;
            unsigned int sc = 1;
            try {
                Json::Value root;
                bool success =  LoadJsonFromFile(FPP_DIR_CONFIG("/plugin.triksc.json"), root);
                
                if (root.isMember("port")) {
                    port = root["port"].asString();
                }
                if (root.isMember("startchannel")) {
                    sc = root["startchannel"].asInt();
                } 

                LogInfo(VB_PLUGIN, "Using %s Serial Output Start Channel %d\n", port.c_str(), sc);
                if(port.empty()) {
                    LogErr(VB_PLUGIN, "Serial Port is empty '%s'\n", port.c_str());
                    return false;
                }
                if(port.find("/dev/") == std::string::npos)
                {
                    port = "/dev/" + port;
                }
                int fd = SerialOpen(port.c_str(), 9600, "8N1", false);
                if (fd < 0) {
                    LogErr(VB_PLUGIN, "Could Not Open Serial Port '%s'\n", port.c_str());
                    return false;
                }
                m_fd = fd;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                tcflush(m_fd,TCIOFLUSH);
                ioctl(m_fd, TCIOFLUSH, 2); 
                LogInfo(VB_PLUGIN, "Serial Input Started\n");
                return true;
            } catch (...) {
                LogErr(VB_PLUGIN, "Could not Initialize Serial Port '%s'\n", port.c_str());
            }                
        }else{
            LogInfo(VB_PLUGIN, "No plugin.serial-event.json config file found\n");
        }
        return false;
    }

    void CloseSerial() {
        if (m_fd >= 0) {
            SerialClose(m_fd);
            m_fd = -1;
        }
    }

    int SerialDataAvailable(int fd) {
        int bytes {0};
        ioctl(fd, FIONREAD, &bytes);
        return bytes;
    }

    int SerialDataRead(int fd, char* buf, size_t len) {
        // Read() (using read() ) will return an 'error' EAGAIN as it is
        // set to non-blocking. This is not a true error within the
        // functionality of Read, and thus should be handled by the caller.
        int n = read(fd, buf, len);
        if((n < 0) && (errno == EAGAIN)) return 0;
        return n;
    }

    void remove_control_characters(std::string& s) {
        s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return std::iscntrl(c); }), s.end());
    }
    

     
};


extern "C" {
    FPPPlugin *createPlugin() {
        return new TriksCPlugin();
    }
}
