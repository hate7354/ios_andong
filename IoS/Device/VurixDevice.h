#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

#include <curl/curl.h>
#include "../json/json.h"

using namespace std;

struct devcam_t {
    std::string serial;
    std::string addr;
    std::string stat_name;
    uint32_t dch_ch;
    uint32_t dchm_serial;
    std::string rtsp_addr;
    bool ptz_lock = false;
};

class CVurixDevice
{
public:
    CVurixDevice();
    ~CVurixDevice();

private:
    std::string _http_url;
    uint16_t _port;

    std::string _id;
    std::string _pass;
    std::string _license;

    std::string _auth_token;
    uint32_t _api_serial;
    uint32_t _user_serial;
    uint32_t _ctx_serial = 0;

    bool _loop = false;
    thread _thr;
    std::condition_variable _cond;
    std::mutex	_mtx, _mtx_p;

    thread _thr_evt;
    CURL* _curl_evt = nullptr;

    std::vector<std::string> _ref_device_list;
    uint32_t _dev_count = 0;

public:
    std::vector<struct devcam_t> _devices;

private:
    static size_t callback(const char* in, size_t size, size_t num, char* out);
    static size_t callbackevent(const char* in, size_t size, size_t num, char* out);
    bool Login(std::string ipaddr, uint16_t port, std::string id, std::string pass, std::string license);
    void Logout();
    bool KeepAlive();
    void KeepAliveThread();
    bool ParseDevChInfo(struct devcam_t* pdev);
    bool ParseDevRtspAddr(struct devcam_t* pdev);

public:
    bool Connect(std::string ipaddr, uint16_t port, std::string id, std::string pass, std::string license);
    void Disconnect();
    bool GetDevInfo();
    void GetEvents();
    bool GetRtspAddr(std::string dev_serial, string& address) const;
    bool GetRtspAddr(std::string dev_serial, string& address, string& host, uint16_t& port, string& uri) const;
    bool SetReferenceDevice(std::string dev_serial);
    void SendEvent(string ipaddr, uint16_t port, std::string dev_serial, int algId, SYSTEMTIME Time);
};
