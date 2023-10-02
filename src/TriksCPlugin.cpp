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
#include <pthread.h>
#include <sstream>
#include <httpserver.hpp>
#include <cmath>
#include <mutex>
#include <regex>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>
#include <memory>

#include <termios.h>

#include "commands/Commands.h"
#include "common.h"
#include "settings.h"
#include "Plugin.h"
#include "log.h"

#include "channeloutput/serialutil.h"

#define TRIKSC_MAX_CHANNELS    9216
#define TRIKSC_PANEL_CHANNELS  2304
#define TRIKSC_MAX_PANELS         4

// These print the byte backwards since that is how the Triks-C needs it
#define BYTETOBINARYPATTERN "%c%c%c%c%c%c%c%c"
#define BYTETOBINARY(byte)  \
	(byte & 0x01 ? 'o' : ' '), \
	(byte & 0x02 ? 'o' : ' '), \
	(byte & 0x04 ? 'o' : ' '), \
	(byte & 0x08 ? 'o' : ' '), \
	(byte & 0x10 ? 'o' : ' '), \
	(byte & 0x20 ? 'o' : ' '), \
	(byte & 0x40 ? 'o' : ' '), \
	(byte & 0x80 ? 'o' : ' ')

struct triksCPrivData
 {
	unsigned char inBuf[TRIKSC_MAX_CHANNELS];
	unsigned char workBuf[TRIKSC_MAX_CHANNELS];
	unsigned char outBuf[TRIKSC_MAX_PANELS][194];
	int           outputBytes[TRIKSC_MAX_PANELS];

	std::string filename;
	int  fd;
	int  width;
	int  height;
	int  panels;
	std::atomic<bool>  threadIsRunning{false};
	std::atomic<bool>  runThread{true};
	std::atomic<bool> dataWaiting{false};

	//pthread_t       processThreadID;
	//pthread_mutex_t bufLock;
	//pthread_mutex_t sendLock;
	//pthread_cond_t  sendCond;
    std::mutex bufLock; 
    std::mutex sendLock; 
    std::thread  processThread;
};

class TriksCPlugin : public FPPPlugin, public httpserver::http_resource {
public:
    bool enabled {false};
    std::unique_ptr<triksCPrivData> data{nullptr};
    //std::thread thr;
  
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

    void TriksC_Dump(TriksCPrivData *privData) {
        LogDebug(VB_PLUGIN, "  privData: %p\n", privData);

        if (!privData)
            return;

        LogDebug(VB_PLUGIN, "    filename       : %s\n", privData->filename);
        LogDebug(VB_PLUGIN, "    fd             : %d\n", privData->fd);
        LogDebug(VB_PLUGIN, "    threadIsRunning: %d\n", privData->threadIsRunning);
        LogDebug(VB_PLUGIN, "    runThread      : %d\n", privData->runThread);
        LogDebug(VB_PLUGIN, "    width          : %d\n", privData->width);
        LogDebug(VB_PLUGIN, "    height         : %d\n", privData->height);
        LogDebug(VB_PLUGIN, "    panels         : %d\n", privData->panels);
    }

    void RunTriksCOutputThread(triksCPrivData *privData)
    {
        LogDebug(VB_PLUGIN, "RunTriksCOutputThread()\n");

        long long wakeTime = GetTime();
       long long lastProcTime = 0;
       long long frameTime = 0;
        //struct timeval  tv;
       // struct timespec ts;

        privData->threadIsRunning = true;
        LogDebug(VB_PLUGIN, "Triks-C output thread started\n");

        int panels = privData->panels;

        while (privData->runThread)
        {
           LogExcess(VB_PLUGIN, "Triks-C output thread: woke\n");


            // See if there is any data waiting to process or if we timed out

            if (privData->dataWaiting)
            {
                ProcessInputBuffer(privData);
                std::this_thread::sleep_for(96000us);
            }
            else
            {
               std::this_thread::sleep_for(10000us);//10ms
            }
        }

        LogDebug(VB_PLUGIN, "Triks-C output thread complete\n");
        privData->threadIsRunning = false;
        return nullptr;
    }

    int TriksC_Open(const char *configStr) {
        LogDebug(VB_PLUGIN, "TriksC_Open('%s')\n", configStr);

        //TriksCPrivData *privData =
        //    (TriksCPrivData *)malloc(sizeof(TriksCPrivData));
       // if (privData == NULL)
       // {
       //     LogErr(VB_PLUGIN, "Error %d allocating private memory: %s\n",
      //          errno, strerror(errno));
       //     
       //     return 0;
       // }
       // bzero(privData, sizeof(TriksCPrivData));
        data = std::make_unique<triksCPrivData>();
        privData->fd = -1;

        char deviceName[32];
        char cfg[1025];

        strncpy(cfg, configStr, 1024);
        char *s = strtok(cfg, ",;");

        strcpy(deviceName, "UNKNOWN");

        while (s) {
            char tmp[128];
            char *div = NULL;

            strcpy(tmp, s);
            div = strchr(tmp, '=');

            if (div) {
                *div = '\0';
                div++;

                if (!strcmp(tmp, "device")) {
                    LogDebug(VB_CHANNELOUT, "Using %s for Triks-C output\n", div);
                    strcpy(deviceName, div);
                } else if (!strcmp(tmp, "layout")) {
                    switch (div[0])
                    {
                        case '1':   privData->width = 1;
                                    break;
                        case '2':   privData->width = 2;
                                    break;
                        case '3':   privData->width = 3;
                                    break;
                        case '4':   privData->width = 4;
                                    break;
                        default:	LogDebug(VB_CHANNELOUT, "Invalid width (%c) in Triks-C layout: %s\n", div[0], div);
                                    return 0;
                    }
                    switch (div[2])
                    {
                        case '1':   privData->height = 1;
                                    break;
                        case '2':   privData->height = 2;
                                    break;
                        case '3':   privData->height = 3;
                                    break;
                        case '4':   privData->height = 4;
                                    break;
                        default:	LogDebug(VB_CHANNELOUT, "Invalid height (%c) in Triks-C layout: %s\n", div[2], div);
                                    return 0;
                    }
                    privData->panels = privData->width * privData->height;
                }
            }
            s = strtok(NULL, ",;");
        }

        if (!strcmp(deviceName, "UNKNOWN"))
        {
            LogErr(VB_PLUGIN, "Invalid Config Str: %s\n", configStr);
            free(privData);
            return 0;
        }

        strcpy(privData->filename, "/dev/");
        strcat(privData->filename, deviceName);

        LogInfo(VB_PLUGIN, "Opening %s for Triks-C output\n",
            privData->filename);

        privData->fd = SerialOpen(privData->filename, 57600, "8N1");
        if (privData->fd < 0)
        {
            LogErr(VB_PLUGIN, "Error %d opening %s: %s\n",
                errno, privData->filename, strerror(errno));

            free(privData);
            return 0;
        }

        //pthread_mutex_init(&privData->bufLock, NULL);
        //pthread_mutex_init(&privData->sendLock, NULL);
        //pthread_cond_init(&privData->sendCond, NULL);
        privData->runThread = 1;

        privData->processThread = std::thread(RunTriksCOutputThread, privData);
        privData->processThread.join();
        //int result = pthread_create(&privData->processThreadID, NULL, &RunTriksCOutputThread, privData);

        return 1;
    }


    int TriksC_Close(void *data) {
        LogDebug(VB_PLUGIN, "TriksC_Close(%p)\n", data);

        privData->runThread = 0;
        SerialClose(privData->fd);
        privData->fd = -1;
        return 0;
    }

    int TriksC_IsConfigured(void) {
        return 0;
    }


    int TriksC_IsActive(void *data) {
        LogDebug(VB_PLUGIN, "TriksC_IsActive(%p)\n", data);
        TriksCPrivData *privData = (TriksCPrivData*)data;

        if (!privData)
            return 0;

        TriksC_Dump(privData);

        if (privData->fd > 0)
            return 1;

        return 0;
    }

    void EncodeAndStore(TriksCPrivData *privData, int panel, unsigned char *ptr)
    {
        unsigned char uc = EncodeBytes(ptr);

        // Escape certain special values
        if (uc == 0x7D) // Pad Byte
        {
            privData->outBuf[panel][privData->outputBytes[panel]++] = 0x7F;
            privData->outBuf[panel][privData->outputBytes[panel]++] = 0x2F;
        }
        else if (uc == 0x7E) // Sync Byte
        {
            privData->outBuf[panel][privData->outputBytes[panel]++] = 0x7F;
            privData->outBuf[panel][privData->outputBytes[panel]++] = 0x30;
        }
        else if (uc == 0x7F) // Escape Byte
        {
            privData->outBuf[panel][privData->outputBytes[panel]++] = 0x7F;
            privData->outBuf[panel][privData->outputBytes[panel]++] = 0x31;
        }
        else
        {
            privData->outBuf[panel][privData->outputBytes[panel]++] = uc;
        }
    }

    void EncodeWorkBuffer(TriksCPrivData *privData)
    {
        int p = 0;
        int y = 0;
        int x = 0;
        unsigned char *ptr = privData->workBuf;

        bzero(privData->outBuf, sizeof(privData->outBuf));
        bzero(privData->outputBytes, sizeof(privData->outputBytes));

        for (p = 0; p < TRIKSC_MAX_PANELS; p++)
        {
            privData->outBuf[p][0] = 0x7E; // Command Sync Byte 
            privData->outputBytes[p] = 2;  // Offset for sync and command bytes
        }

        privData->outBuf[0][1] = 0x8D; // CMD_FRAME for Panel #1
        privData->outBuf[1][1] = 0x95; // CMD_FRAME for Panel #2
        privData->outBuf[2][1] = 0xA5; // CMD_FRAME for Panel #3
        privData->outBuf[3][1] = 0xC5; // CMD_FRAME for Panel #4

        if (privData->width == 1) // Handle 1x? layouts
        {
            for (p = 0; p < privData->panels; p++)
            {
                ptr = privData->workBuf + ((p + 1) * TRIKSC_PANEL_CHANNELS) - 24;
                for (y = 15; y >= 0; y--)
                {
                    for (x = 5; x >= 0; x--)
                    {
                        EncodeAndStore(privData, p, ptr);

                        ptr -= 24;
                    }
                }
            }
        }
        else if (privData->height == 1) // Handle ?x1 layouts
        {
            ptr = privData->workBuf + (privData->panels * TRIKSC_PANEL_CHANNELS) - 24;
            for (y = 15; y >= 0; y--)
            {
                for (x = (privData->width * 6) - 1; x >= 0; x--)
                {
                    p = x / 6;

                    EncodeAndStore(privData, p, ptr);

                    ptr -= 24;
                }
            }
        }
        else // Must be a 2x2 layout
        {
            // Handle as 2 rows of 2 to make it easier, bottom then top
            int r = 0;
            for (r = 1; r >= 0; r--)
            {
                ptr = privData->workBuf + ((2 * r + 2) * TRIKSC_PANEL_CHANNELS) - 24;
                for (y = 15; y >= 0; y--)
                {
                    for (x = 11; x >= 0; x--)
                    {
                        p = x / 6;

                        if (r)
                            p += 2;

                        EncodeAndStore(privData, p, ptr);

                        ptr -= 24;
                    }
                }
            }
        }
    }


    void ProcessInputBuffer(TriksCPrivData *privData)
    {
        privData->bufLock.lock();
        memcpy(privData->workBuf, privData->inBuf, TRIKSC_MAX_CHANNELS);
        privData->dataWaiting = 0;
        privData->bufLock.unlock();

        EncodeWorkBuffer(privData);

        //if (LogMaskIsSet(VB_CHANNELDATA) && LogLevelIsSet(LOG_EXCESSIVE))
        //    DumpEncodedBuffer(privData);

        for (int p = 0; p < privData->panels; p++)
        {
            write(privData->fd, privData->outBuf[p], privData->outputBytes[p]);
        }
    }

    int TriksC_SendData(void *data, const char *channelData, int channelCount)
    {
        LogDebug(VB_PLUGIN, "TriksC_SendData(%p, %p, %d)\n",
            data, channelData, channelCount);

        TriksCPrivData *privData = (TriksCPrivData*)data;

        if (channelCount <= TRIKSC_MAX_CHANNELS) {
            bzero(privData->inBuf, TRIKSC_MAX_CHANNELS);
        } else {
            LogErr(VB_PLUGIN,
                "TriksC_SendData() tried to send %d bytes when max is %d\n",
                channelCount, TRIKSC_MAX_CHANNELS);
            return 0;
        }

        // Copy latest data to our input buffer for processing
        privData->bufLock.lock();
        memcpy(privData->inBuf, channelData, channelCount);
        privData->dataWaiting = true;
        privData->bufLock.unlock();

        //if (privData->threadIsRunning)
        //    pthread_cond_signal(&privData->sendCond);
        //else
        //    ProcessInputBuffer(privData);
        return channelCount;
    }

    /*
    *
    */
    int TriksC_MaxChannels(void *data)
    {
        return TRIKSC_MAX_CHANNELS;
    }
    
unsigned char EncodeBytes(unsigned char *ptr)
{
	unsigned char result = 0;

	if (*ptr)
	{
		ptr += 3;
		// bit 7 ON
		if (*ptr)
		{
			ptr += 3;
			// bit 6 ON
			if (*ptr)
			{
				ptr += 3;
				// bit 5 ON
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111111;
								}
								else
								{
									// bit 0 Off
									result = 0b01111111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111111;
								}
								else
								{
									// bit 0 Off
									result = 0b00111111;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011111;
								}
								else
								{
									// bit 0 Off
									result = 0b01011111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011111;
								}
								else
								{
									// bit 0 Off
									result = 0b00011111;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101111;
								}
								else
								{
									// bit 0 Off
									result = 0b01101111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101111;
								}
								else
								{
									// bit 0 Off
									result = 0b00101111;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001111;
								}
								else
								{
									// bit 0 Off
									result = 0b01001111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001111;
								}
								else
								{
									// bit 0 Off
									result = 0b00001111;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110111;
								}
								else
								{
									// bit 0 Off
									result = 0b01110111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110111;
								}
								else
								{
									// bit 0 Off
									result = 0b00110111;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010111;
								}
								else
								{
									// bit 0 Off
									result = 0b01010111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010111;
								}
								else
								{
									// bit 0 Off
									result = 0b00010111;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100111;
								}
								else
								{
									// bit 0 Off
									result = 0b01100111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100111;
								}
								else
								{
									// bit 0 Off
									result = 0b00100111;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000111;
								}
								else
								{
									// bit 0 Off
									result = 0b01000111;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000111;
								}
								else
								{
									// bit 0 Off
									result = 0b00000111;
								}
							}
						}
					}
				}
			}
			else
			{
				ptr += 3;
				// bit 5 Off
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111011;
								}
								else
								{
									// bit 0 Off
									result = 0b01111011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111011;
								}
								else
								{
									// bit 0 Off
									result = 0b00111011;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011011;
								}
								else
								{
									// bit 0 Off
									result = 0b01011011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011011;
								}
								else
								{
									// bit 0 Off
									result = 0b00011011;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101011;
								}
								else
								{
									// bit 0 Off
									result = 0b01101011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101011;
								}
								else
								{
									// bit 0 Off
									result = 0b00101011;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001011;
								}
								else
								{
									// bit 0 Off
									result = 0b01001011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001011;
								}
								else
								{
									// bit 0 Off
									result = 0b00001011;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110011;
								}
								else
								{
									// bit 0 Off
									result = 0b01110011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110011;
								}
								else
								{
									// bit 0 Off
									result = 0b00110011;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010011;
								}
								else
								{
									// bit 0 Off
									result = 0b01010011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010011;
								}
								else
								{
									// bit 0 Off
									result = 0b00010011;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100011;
								}
								else
								{
									// bit 0 Off
									result = 0b01100011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100011;
								}
								else
								{
									// bit 0 Off
									result = 0b00100011;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000011;
								}
								else
								{
									// bit 0 Off
									result = 0b01000011;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000011;
								}
								else
								{
									// bit 0 Off
									result = 0b00000011;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			ptr += 3;
			// bit 6 Off
			if (*ptr)
			{
				ptr += 3;
				// bit 5 ON
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111101;
								}
								else
								{
									// bit 0 Off
									result = 0b01111101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111101;
								}
								else
								{
									// bit 0 Off
									result = 0b00111101;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011101;
								}
								else
								{
									// bit 0 Off
									result = 0b01011101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011101;
								}
								else
								{
									// bit 0 Off
									result = 0b00011101;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101101;
								}
								else
								{
									// bit 0 Off
									result = 0b01101101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101101;
								}
								else
								{
									// bit 0 Off
									result = 0b00101101;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001101;
								}
								else
								{
									// bit 0 Off
									result = 0b01001101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001101;
								}
								else
								{
									// bit 0 Off
									result = 0b00001101;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110101;
								}
								else
								{
									// bit 0 Off
									result = 0b01110101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110101;
								}
								else
								{
									// bit 0 Off
									result = 0b00110101;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010101;
								}
								else
								{
									// bit 0 Off
									result = 0b01010101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010101;
								}
								else
								{
									// bit 0 Off
									result = 0b00010101;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100101;
								}
								else
								{
									// bit 0 Off
									result = 0b01100101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100101;
								}
								else
								{
									// bit 0 Off
									result = 0b00100101;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000101;
								}
								else
								{
									// bit 0 Off
									result = 0b01000101;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000101;
								}
								else
								{
									// bit 0 Off
									result = 0b00000101;
								}
							}
						}
					}
				}
			}
			else
			{
				ptr += 3;
				// bit 5 Off
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111001;
								}
								else
								{
									// bit 0 Off
									result = 0b01111001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111001;
								}
								else
								{
									// bit 0 Off
									result = 0b00111001;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011001;
								}
								else
								{
									// bit 0 Off
									result = 0b01011001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011001;
								}
								else
								{
									// bit 0 Off
									result = 0b00011001;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101001;
								}
								else
								{
									// bit 0 Off
									result = 0b01101001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101001;
								}
								else
								{
									// bit 0 Off
									result = 0b00101001;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001001;
								}
								else
								{
									// bit 0 Off
									result = 0b01001001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001001;
								}
								else
								{
									// bit 0 Off
									result = 0b00001001;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110001;
								}
								else
								{
									// bit 0 Off
									result = 0b01110001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110001;
								}
								else
								{
									// bit 0 Off
									result = 0b00110001;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010001;
								}
								else
								{
									// bit 0 Off
									result = 0b01010001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010001;
								}
								else
								{
									// bit 0 Off
									result = 0b00010001;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100001;
								}
								else
								{
									// bit 0 Off
									result = 0b01100001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100001;
								}
								else
								{
									// bit 0 Off
									result = 0b00100001;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000001;
								}
								else
								{
									// bit 0 Off
									result = 0b01000001;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000001;
								}
								else
								{
									// bit 0 Off
									result = 0b00000001;
								}
							}
						}
					}
				}
			}
		}
	}
	else
	{
		ptr += 3;
		// bit 7 Off
		if (*ptr)
		{
			ptr += 3;
			// bit 6 ON
			if (*ptr)
			{
				ptr += 3;
				// bit 5 ON
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111110;
								}
								else
								{
									// bit 0 Off
									result = 0b01111110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111110;
								}
								else
								{
									// bit 0 Off
									result = 0b00111110;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011110;
								}
								else
								{
									// bit 0 Off
									result = 0b01011110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011110;
								}
								else
								{
									// bit 0 Off
									result = 0b00011110;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101110;
								}
								else
								{
									// bit 0 Off
									result = 0b01101110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101110;
								}
								else
								{
									// bit 0 Off
									result = 0b00101110;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001110;
								}
								else
								{
									// bit 0 Off
									result = 0b01001110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001110;
								}
								else
								{
									// bit 0 Off
									result = 0b00001110;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110110;
								}
								else
								{
									// bit 0 Off
									result = 0b01110110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110110;
								}
								else
								{
									// bit 0 Off
									result = 0b00110110;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010110;
								}
								else
								{
									// bit 0 Off
									result = 0b01010110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010110;
								}
								else
								{
									// bit 0 Off
									result = 0b00010110;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100110;
								}
								else
								{
									// bit 0 Off
									result = 0b01100110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100110;
								}
								else
								{
									// bit 0 Off
									result = 0b00100110;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000110;
								}
								else
								{
									// bit 0 Off
									result = 0b01000110;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000110;
								}
								else
								{
									// bit 0 Off
									result = 0b00000110;
								}
							}
						}
					}
				}
			}
			else
			{
				ptr += 3;
				// bit 5 Off
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111010;
								}
								else
								{
									// bit 0 Off
									result = 0b01111010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111010;
								}
								else
								{
									// bit 0 Off
									result = 0b00111010;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011010;
								}
								else
								{
									// bit 0 Off
									result = 0b01011010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011010;
								}
								else
								{
									// bit 0 Off
									result = 0b00011010;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101010;
								}
								else
								{
									// bit 0 Off
									result = 0b01101010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101010;
								}
								else
								{
									// bit 0 Off
									result = 0b00101010;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001010;
								}
								else
								{
									// bit 0 Off
									result = 0b01001010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001010;
								}
								else
								{
									// bit 0 Off
									result = 0b00001010;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110010;
								}
								else
								{
									// bit 0 Off
									result = 0b01110010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110010;
								}
								else
								{
									// bit 0 Off
									result = 0b00110010;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010010;
								}
								else
								{
									// bit 0 Off
									result = 0b01010010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010010;
								}
								else
								{
									// bit 0 Off
									result = 0b00010010;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100010;
								}
								else
								{
									// bit 0 Off
									result = 0b01100010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100010;
								}
								else
								{
									// bit 0 Off
									result = 0b00100010;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000010;
								}
								else
								{
									// bit 0 Off
									result = 0b01000010;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000010;
								}
								else
								{
									// bit 0 Off
									result = 0b00000010;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			ptr += 3;
			// bit 6 Off
			if (*ptr)
			{
				ptr += 3;
				// bit 5 ON
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111100;
								}
								else
								{
									// bit 0 Off
									result = 0b01111100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111100;
								}
								else
								{
									// bit 0 Off
									result = 0b00111100;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011100;
								}
								else
								{
									// bit 0 Off
									result = 0b01011100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011100;
								}
								else
								{
									// bit 0 Off
									result = 0b00011100;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101100;
								}
								else
								{
									// bit 0 Off
									result = 0b01101100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101100;
								}
								else
								{
									// bit 0 Off
									result = 0b00101100;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001100;
								}
								else
								{
									// bit 0 Off
									result = 0b01001100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001100;
								}
								else
								{
									// bit 0 Off
									result = 0b00001100;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110100;
								}
								else
								{
									// bit 0 Off
									result = 0b01110100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110100;
								}
								else
								{
									// bit 0 Off
									result = 0b00110100;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010100;
								}
								else
								{
									// bit 0 Off
									result = 0b01010100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010100;
								}
								else
								{
									// bit 0 Off
									result = 0b00010100;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100100;
								}
								else
								{
									// bit 0 Off
									result = 0b01100100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100100;
								}
								else
								{
									// bit 0 Off
									result = 0b00100100;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000100;
								}
								else
								{
									// bit 0 Off
									result = 0b01000100;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000100;
								}
								else
								{
									// bit 0 Off
									result = 0b00000100;
								}
							}
						}
					}
				}
			}
			else
			{
				ptr += 3;
				// bit 5 Off
				if (*ptr)
				{
					ptr += 3;
					// bit 4 ON
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11111000;
								}
								else
								{
									// bit 0 Off
									result = 0b01111000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10111000;
								}
								else
								{
									// bit 0 Off
									result = 0b00111000;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11011000;
								}
								else
								{
									// bit 0 Off
									result = 0b01011000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10011000;
								}
								else
								{
									// bit 0 Off
									result = 0b00011000;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11101000;
								}
								else
								{
									// bit 0 Off
									result = 0b01101000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10101000;
								}
								else
								{
									// bit 0 Off
									result = 0b00101000;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11001000;
								}
								else
								{
									// bit 0 Off
									result = 0b01001000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10001000;
								}
								else
								{
									// bit 0 Off
									result = 0b00001000;
								}
							}
						}
					}
				}
				else
				{
					ptr += 3;
					// bit 4 Off
					if (*ptr)
					{
						ptr += 3;
						// bit 3 ON
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11110000;
								}
								else
								{
									// bit 0 Off
									result = 0b01110000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10110000;
								}
								else
								{
									// bit 0 Off
									result = 0b00110000;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11010000;
								}
								else
								{
									// bit 0 Off
									result = 0b01010000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10010000;
								}
								else
								{
									// bit 0 Off
									result = 0b00010000;
								}
							}
						}
					}
					else
					{
						ptr += 3;
						// bit 3 Off
						if (*ptr)
						{
							ptr += 3;
							// bit 2 ON
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11100000;
								}
								else
								{
									// bit 0 Off
									result = 0b01100000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10100000;
								}
								else
								{
									// bit 0 Off
									result = 0b00100000;
								}
							}
						}
						else
						{
							ptr += 3;
							// bit 2 Off
							if (*ptr)
							{
								ptr += 3;
								// bit 1 ON
								if (*ptr)
								{
									// bit 0 ON
									result = 0b11000000;
								}
								else
								{
									// bit 0 Off
									result = 0b01000000;
								}
							}
							else
							{
								ptr += 3;
								// bit 1 Off
								if (*ptr)
								{
									// bit 0 ON
									result = 0b10000000;
								}
								else
								{
									// bit 0 Off
									result = 0b00000000;
								}
							}
						}
					}
				}
			}
		}
	}

	return result;
}
     
};


extern "C" {
    FPPPlugin *createPlugin() {
        return new TriksCPlugin();
    }
}
