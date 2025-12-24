#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <string>
#include <vector>
#include <climits>

#include "ntp-client.hpp"
#include "ini_funcs.hpp"

TimeServiceType __nx_time_service_type = TimeServiceType_System;

const char* iniLocations[] = {
    "/config/quickntp.ini",
    "/config/quickntp/config.ini",
    "/switch/.overlays/quickntp.ini",
};
const char* iniSection = "Servers";

const char* defaultServerAddress = "pool.ntp.org";
const char* defaultServerName = "NTP Pool Main";

class NtpGui : public tsl::Gui {
private:
    int currentServer = 0;
    bool blockFlag = false;
    std::vector<std::string> serverAddresses;
    std::vector<std::string> serverNames;

    std::string getCurrentServerAddress() {
        return serverAddresses[currentServer];
    }

    bool setNetworkSystemClock(time_t time) {
        Result rs = timeSetCurrentTime(TimeType_NetworkSystemClock, (uint64_t)time);
        return R_SUCCEEDED(rs);
    }

    void setTime() {
        std::string srv = getCurrentServerAddress();
        NTPClient* client = new NTPClient(srv.c_str());

        time_t ntpTime = client->getTime();
        
        if (ntpTime != 0) {
            if (setNetworkSystemClock(ntpTime)) {
                if (tsl::notification)
                    tsl::notification->showNow(ult::NOTIFY_HEADER+"从 " + srv + " 同步", 22);
            } else {
                if (tsl::notification)
                    tsl::notification->showNow(ult::NOTIFY_HEADER+"无法同步网络时间.", 22);
            }
        } else {
            if (tsl::notification)
                tsl::notification->showNow(ult::NOTIFY_HEADER+"错误: 无法获取 NTP 时间", 22);
        }

        delete client;
    }

    void setNetworkTimeAsUser() {
        time_t userTime, netTime;

        Result rs = timeGetCurrentTime(TimeType_UserSystemClock, (u64*)&userTime);
        if (R_FAILED(rs)) {
            if (tsl::notification)
                tsl::notification->show(ult::NOTIFY_HEADER+"获取本地时间 " + std::to_string(rs), 22);
            return;
        }

        std::string usr = "设置用户时间成功!";
        std::string gr8 = "";
        rs = timeGetCurrentTime(TimeType_NetworkSystemClock, (u64*)&netTime);
        if (R_SUCCEEDED(rs) && netTime < userTime) {
            gr8 = " Great Scott!";
        }

        if (setNetworkSystemClock(userTime)) {
            if (tsl::notification)
                tsl::notification->showNow(ult::NOTIFY_HEADER+usr + gr8, 22);
        } else {
            if (tsl::notification)
                tsl::notification->showNow(ult::NOTIFY_HEADER+"无法设置网络时间.", 22);
        }
    }

    void getOffset() {
        time_t currentTime;
        Result rs = timeGetCurrentTime(TimeType_NetworkSystemClock, (u64*)&currentTime);
        if (R_FAILED(rs)) {
            if (tsl::notification)
                tsl::notification->showNow(ult::NOTIFY_HEADER+"获取网络时间 " + std::to_string(rs), 22);
            return;
        }

        std::string srv = getCurrentServerAddress();
        NTPClient* client = new NTPClient(srv.c_str());

        time_t ntpTimeOffset = client->getTimeOffset(currentTime);
        
        if (ntpTimeOffset != LLONG_MIN) {
            if (tsl::notification)
                tsl::notification->showNow(ult::NOTIFY_HEADER+"偏移: " + std::to_string(ntpTimeOffset) + "s", 22);
        } else {
            if (tsl::notification)
                tsl::notification->showNow(ult::NOTIFY_HEADER+"失败: 获取偏移量失败", 22);
        }

        delete client;
    }

    bool operationBlock(std::function<void()> fn) {
        if (!blockFlag) {
            blockFlag = true;
            fn();
            blockFlag = false;
        }
        return !blockFlag;
    }

    std::function<std::function<bool(u64 keys)>(int key)> syncListener = [this](int key) {
        return [=, this](u64 keys) {
            if (keys & key) {
                return operationBlock([&]() {
                    setTime();
                });
            }
            return false;
        };
    };

    std::function<std::function<bool(u64 keys)>(int key)> offsetListener = [this](int key) {
        return [=, this](u64 keys) {
            if (keys & key) {
                return operationBlock([&]() {
                    getOffset();
                });
            }
            return false;
        };
    };

public:
    NtpGui() {
        std::string iniFile = iniLocations[0];
        
        // Find the first existing INI file
        for (const char* loc : iniLocations) {
            if (ult::isFileOrDirectory(loc)) {
                iniFile = loc;
                break;
            }
        }

        // Get all key-value pairs from the Servers section
        auto serverMap = ult::getKeyValuePairsFromSection(iniFile, iniSection);

        // Populate server lists from the parsed data
        for (const auto& [key, value] : serverMap) {
            serverAddresses.push_back(value);
            
            std::string keyStr = key;
            std::replace(keyStr.begin(), keyStr.end(), '_', ' ');
            serverNames.push_back(keyStr);
        }

        // Add default server if none were found
        if (serverNames.empty() || serverAddresses.empty()) {
            serverNames.push_back(defaultServerName);
            serverAddresses.push_back(defaultServerAddress);
        }
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("时间校准", std::string("南宫镜 ") + APP_VERSION);
        frame->m_showWidget = true;

        auto list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("选择服务器 "+ult::DIVIDER_SYMBOL+" \uE0E0  同步 "+ult::DIVIDER_SYMBOL+" \uE0E3  偏移"));

        // Create NamedStepTrackBar with V2 style using the server names
        tsl::elm::NamedStepTrackBar* trackbar;
        if (!serverNames.empty()) {
            // Build initializer list from vector
            switch (serverNames.size()) {
                case 1:
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0]}, true, "服务器");
                    break;
                case 2:
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0], serverNames[1]}, true, "服务器");
                    break;
                case 3:
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0], serverNames[1], serverNames[2]}, true, "服务器");
                    break;
                case 4:
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0], serverNames[1], serverNames[2], serverNames[3]}, true, "服务器");
                    break;
                case 5:
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0], serverNames[1], serverNames[2], serverNames[3], serverNames[4]}, true, "服务器");
                    break;
                case 6:
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0], serverNames[1], serverNames[2], serverNames[3], serverNames[4], serverNames[5]}, true, "服务器");
                    break;
                case 7:
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0], serverNames[1], serverNames[2], serverNames[3], serverNames[4], serverNames[5], serverNames[6]}, true, "服务器");
                    break;
                default:
                    // 超过 7 个服务器, 只使用前 7 个
                    trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {serverNames[0], serverNames[1], serverNames[2], serverNames[3], serverNames[4], serverNames[5], serverNames[6]}, true, "服务器");
                    break;
            }
        } else {
            trackbar = new tsl::elm::NamedStepTrackBar("\uE017", {defaultServerName}, true, "服务器");
        }
        
        trackbar->setValueChangedListener([this](u8 val) {
            currentServer = val;
        });
        trackbar->setClickListener([this, trackbar](u64 keys) {
            static bool wasTriggered = false;
            
            // Only trigger animation on initial press (keys down), not while held
            if (((keys & HidNpadButton_A) || (keys & HidNpadButton_Y)) && !wasTriggered) {
                trackbar->triggerClickAnimation();
                triggerEnterFeedback();
                wasTriggered = true;
            }
            
            // Reset flag when key is released
            if (!(keys & HidNpadButton_A) && !(keys & HidNpadButton_Y)) {
                wasTriggered = false;
            }
            
            return syncListener(HidNpadButton_A)(keys) || offsetListener(HidNpadButton_Y)(keys);
        });
        list->addItem(trackbar);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {}), 24);

        auto* syncTimeItem = new tsl::elm::ListItem("同步时间");
        syncTimeItem->setClickListener(syncListener(HidNpadButton_A));
        list->addItem(syncTimeItem);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
                          renderer->drawString("使用所选服务器同步时间.", false, x + 20, y + 26, 15, renderer->a(tsl::style::color::ColorDescription));
                      }),
                      50);

        auto* getOffsetItem = new tsl::elm::ListItem("获取偏移");
        getOffsetItem->setClickListener(offsetListener(HidNpadButton_A));
        list->addItem(getOffsetItem);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
                          renderer->drawString("查看所选服务器时间偏移量.\n\n\uE016  ±3秒以内的差异是正常的.", false, x + 20, y + 26, 15, renderer->a(tsl::style::color::ColorDescription));
                      }),
                      70);

        auto* setToInternalItem = new tsl::elm::ListItem("用户时间");
        setToInternalItem->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                return operationBlock([&]() {
                    setNetworkTimeAsUser();
                });
            }
            return false;
        });
        list->addItem(setToInternalItem);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
                          renderer->drawString("将网络时间设置为用户时间.", false, x + 20, y + 26, 15, renderer->a(tsl::style::color::ColorDescription));
                      }),
                      50);

        frame->setContent(list);
        return frame;
    }
};

class NtpOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        constexpr SocketInitConfig socketInitConfig = {
            // TCP buffers
            .tcp_tx_buf_size     = 16 * 1024,   // 16 KB default
            .tcp_rx_buf_size     = 16 * 1024*2,   // 16 KB default
            .tcp_tx_buf_max_size = 64 * 1024,   // 64 KB default max
            .tcp_rx_buf_max_size = 64 * 1024*2,   // 64 KB default max
            
            // UDP buffers
            .udp_tx_buf_size     = 512,         // 512 B default
            .udp_rx_buf_size     = 512,         // 512 B default
        
            // Socket buffer efficiency
            .sb_efficiency       = 1,           // 0 = default, balanced memory vs CPU
                                                // 1 = prioritize memory efficiency (smaller internal allocations)
            .bsd_service_type    = BsdServiceType_Auto // Auto-select service
        };
        socketInitialize(&socketInitConfig);
        ASSERT_FATAL(nifmInitialize(NifmServiceType_User));
        ASSERT_FATAL(timeInitialize());
        ASSERT_FATAL(smInitialize());
    }

    virtual void exitServices() override {
        socketExit();
        nifmExit();
        timeExit();
        smExit();
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<NtpGui>();
    }
};

int main(int argc, char** argv) {
    return tsl::loop<NtpOverlay>(argc, argv);
}
