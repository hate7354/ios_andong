#include "pch.h"
#include "VurixDevice.h"

CVurixDevice* g_pVurixDevInfo = nullptr;

CVurixDevice::CVurixDevice()
{
    g_pVurixDevInfo = this;
}

CVurixDevice::~CVurixDevice()
{
    if (_loop) {
        _loop = false;
        _cond.notify_one();
        _thr.join();
    }

    if (_dev_count > 0) {
        curl_easy_setopt(_curl_evt, CURLOPT_TIMEOUT_MS, 1);
        _thr_evt.join();
        curl_easy_cleanup(_curl_evt);
        _curl_evt = nullptr;
        
        _dev_count = 0;
    }

    Logout();

    g_pVurixDevInfo = nullptr;
}

size_t CVurixDevice::callback(const char* in, size_t size, size_t num, char* out)
{
    std::string data(in, size * num);
    *((std::stringstream*)out) << data;
    return size * num;
}

size_t CVurixDevice::callbackevent(const char* in, size_t size, size_t num, char* out)
{
    uint32_t pos = 0;
    uint32_t start, end;
    int count;

    do {
        start = end = 0;
        for (; pos < size * num; pos++) {
            if (in[pos] == '{')
                break;
        }

        start = end = pos;
        count = 0;
        for (; pos < size * num; pos++) {
            if (in[pos] == '{')
                count++;
            else if (in[pos] == '}')
                count--;

            if (count == 0) {
                end = pos;
                break;
            }
        }

        if (start != end) {
            std::string data(&in[start], end - start + 1);
            Json::CharReaderBuilder b;
            Json::CharReader* reader(b.newCharReader());
            Json::Value root;
            JSONCPP_STRING errs;

            bool ret = reader->parse(data.c_str(), data.c_str() + data.length(), &root, &errs);
            if (ret && errs.size() == 0) {
                if (root.isMember("cmd")) {
                    std::string value = root["cmd"].asString();
                    if (value.compare("PTZ_LOCK_STATUS") == 0) {
                        std::string dev_serial = root["dev_serial"].asString();
                        uint32_t dch_ch = root["dch_ch"].asInt();
                        std::string st_value = root["status"].asString();
                        bool status = false;
                        if (st_value.compare("LOCK") == 0 || st_value.compare("lock") == 0)
                            status = true;

                        for (vector<struct devcam_t>::iterator iter = g_pVurixDevInfo->_devices.begin(); iter != g_pVurixDevInfo->_devices.end(); iter++) {
                            if (iter->serial == dev_serial && iter->dch_ch == dch_ch) {
                                iter->ptz_lock = status;
                                break;
                            }
                        }
                    }
                }
            }
            delete reader;
        }
    } while (pos < size * num);

    return size * num;
}

bool CVurixDevice::Login(std::string ipaddr, uint16_t port, std::string id, std::string pass, std::string license)
{
    bool bRet = false;

    std::unique_lock<std::mutex> lock(_mtx_p);

    std::string http_url = "http://" + ipaddr;
    const std::string url = http_url +":" + std::to_string(port) + "/api/login";
    CURL* curl = curl_easy_init();

    if (curl) {
        struct curl_slist* chunk = nullptr;
    
        std::string header = "Accept: application/json";
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-account-id: " + id;
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-account-pass: " + pass;
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-account-group: group1";
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-license: " + license;
        chunk = curl_slist_append(chunk, header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        long httpCode(0);
        std::stringstream root;

        // Hook up data handling function.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CVurixDevice::callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &root);

        // Run our HTTP GET command, capture the HTTP response code, and clean up.
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (httpCode == 200) {
                Json::Value jsonData;
                Json::CharReaderBuilder jsonReader;

                if (Json::parseFromStream(jsonReader, root, &jsonData, nullptr)) {
                    if (jsonData["code"].asInt() == 200) {
                        std::stringstream httpData(jsonData["results"].toStyledString());
                        if (Json::parseFromStream(jsonReader, httpData, &jsonData, nullptr)) {
                            _api_serial = jsonData["api_serial"].asInt();
                            _auth_token = jsonData["auth_token"].asString();
                            _user_serial = jsonData["user_serial"].asInt();

                            bRet = true;
                        }
                    }
                }
            }
         }

        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);
    }

     return bRet;
}

void CVurixDevice::Logout()
{
    if (_auth_token.empty())
        return;

    std::unique_lock<std::mutex> lock(_mtx_p);

    const std::string url = _http_url + ":" + std::to_string(_port) + "/api/logout";
    CURL* curl = curl_easy_init();

    if (curl) {
        struct curl_slist* chunk = nullptr;

        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        std::string header = "Accept: application/json";
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-auth-token: " + _auth_token;
        chunk = curl_slist_append(chunk, header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        long httpCode(0);
        std::stringstream root;

        // Hook up data handling function.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CVurixDevice::callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &root);

        curl_easy_perform(curl);

        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);
    }
}

bool CVurixDevice::Connect(std::string ipaddr, uint16_t port, std::string id, std::string pass, std::string license)
{
    bool bRet = Login(ipaddr, port, id, pass, license);
    if (bRet) {
        _http_url = "http://" + ipaddr;
        _port = port;
        _id = id;
        _pass = pass;
        _license = license;

        _loop = true;
        _thr = thread{ &CVurixDevice::KeepAliveThread, this };
    }

    return bRet;
}

void CVurixDevice::Disconnect()
{
    if (_loop) {
        _loop = false;
        _cond.notify_one();
        _thr.join();
    }

    if (_dev_count > 0) {
        curl_easy_setopt(_curl_evt, CURLOPT_TIMEOUT_MS, 1);
        _thr_evt.join();
        curl_easy_cleanup(_curl_evt);
        _curl_evt = nullptr;
        _dev_count = 0;
        _ref_device_list.clear();
    }

    Logout();
}

bool CVurixDevice::GetDevInfo()
{
    bool bRet = false;

    if (_auth_token.empty())
        return false;

    std::unique_lock<std::mutex> lock(_mtx_p);

    const std::string url = _http_url + ":" + std::to_string(_port) + "/api/device/list/" + std::to_string(_user_serial) + "/" + std::to_string(_ctx_serial);
    CURL* curl = curl_easy_init();

    if (curl) {
        _api_serial++;

        struct curl_slist* chunk = nullptr;

        std::string header = "Accept: application/json";
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-auth-token: " + _auth_token;
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-api-serial: " + std::to_string(_api_serial);
        chunk = curl_slist_append(chunk, header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        long httpCode(0);
        std::stringstream root;

        // Hook up data handling function.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CVurixDevice::callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &root);

        // Run our HTTP GET command, capture the HTTP response code, and clean up.
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (httpCode == 200) {
                Json::Value jsonData;
                Json::CharReaderBuilder jsonReader;

                if (Json::parseFromStream(jsonReader, root, &jsonData, nullptr)) {
                    if (jsonData["code"].asInt() == 200) {
                        bRet = true;
                        std::stringstream httpData(jsonData["results"].toStyledString());
                        if (Json::parseFromStream(jsonReader, httpData, &jsonData, nullptr)) {
                            Json::Value devList = jsonData["tree"];
                            for (Json::ValueIterator it = devList.begin(); it != devList.end(); it++) {
                                if (it->isObject()) {
                                    struct devcam_t device;
                                    device.serial = (*it)["dev_serial"].asString();
                                    device.addr = (*it)["dev_addr"].asString();
                                    device.stat_name = (*it)["stat_name"].asString();
                                    for (std::vector<std::string>::iterator iter = _ref_device_list.begin(); iter != _ref_device_list.end(); iter++) {
                                        if (*iter == device.serial) {
                                            _devices.push_back(device);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);

        _dev_count = static_cast<uint32_t>(_devices.size());

        if (_dev_count > 0) {
            for (vector<struct devcam_t>::iterator iter = _devices.begin(); iter != _devices.end(); iter++) {
                ParseDevChInfo(&*iter);
                ParseDevRtspAddr(&*iter);
            }

            _thr_evt = thread{ &CVurixDevice::GetEvents, this };
        }
    }

    return bRet;
}

bool CVurixDevice::ParseDevChInfo(struct devcam_t *pdev)
{
    bool bRet = false;

    const std::string url = _http_url + ":" + std::to_string(_port) + "/api/device/info/" + pdev->serial;
    CURL* curl = curl_easy_init();

    if (curl) {
        _api_serial++;

        struct curl_slist* chunk = nullptr;

        std::string header = "Accept: application/json";
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-auth-token: " + _auth_token;
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-api-serial: " + std::to_string(_api_serial);
        chunk = curl_slist_append(chunk, header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        long httpCode(0);
        std::stringstream root;

        // Hook up data handling function.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CVurixDevice::callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &root);

        // Run our HTTP GET command, capture the HTTP response code, and clean up.
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (httpCode == 200) {
                Json::Value jsonData;
                Json::CharReaderBuilder jsonReader;

                if (Json::parseFromStream(jsonReader, root, &jsonData, nullptr)) {
                    if (jsonData["code"].asInt() == 200) {
                        for (uint32_t i = 0; i < jsonData["results"].size(); i++) {
                            std::stringstream httpData(jsonData["results"][i].toStyledString());
                            if (Json::parseFromStream(jsonReader, httpData, &jsonData, nullptr)) {
                                for (uint32_t j = 0; j < jsonData["dch"].size(); j++) {
                                    std::stringstream httpData2(jsonData["dch"][j].toStyledString());
                                    if (Json::parseFromStream(jsonReader, httpData2, &jsonData, nullptr)) {
                                        if (jsonData["dch_type"].asInt() == 0) {
                                            bRet = true;

                                            pdev->dch_ch = jsonData["dch_ch"].asInt();

                                            if (jsonData["med"].size() >= 1) {
                                                int med_idx = 0;
                                                if (jsonData["med"].size() >= 2)
                                                    med_idx = 1;

                                                std::stringstream httpData3(jsonData["med"][med_idx].toStyledString());
                                                if (Json::parseFromStream(jsonReader, httpData3, &jsonData, nullptr)) {
                                                    pdev->dchm_serial = jsonData["dchm_serial"].asInt();
                                                }
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                            if (bRet)
                                break;
                        }
                    }
                }
            }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);
    }

    return bRet;
}

bool CVurixDevice::ParseDevRtspAddr(struct devcam_t* pdev)
{
    bool bRet = false;

    const std::string url = _http_url + ":" + std::to_string(_port) + "/api/video/rtsp-url/" + pdev->serial + "/" + std::to_string(pdev->dch_ch) + "/" + std::to_string(pdev->dchm_serial);
    CURL* curl = curl_easy_init();

    if (curl) {
        _api_serial++;

        struct curl_slist* chunk = nullptr;

        std::string header = "Accept: application/json";
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-auth-token: " + _auth_token;
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-api-serial: " + std::to_string(_api_serial);
        chunk = curl_slist_append(chunk, header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        long httpCode(0);
        std::stringstream root;

        // Hook up data handling function.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CVurixDevice::callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &root);

        // Run our HTTP GET command, capture the HTTP response code, and clean up.
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (httpCode == 200) {
                Json::Value jsonData;
                Json::CharReaderBuilder jsonReader;

                if (Json::parseFromStream(jsonReader, root, &jsonData, nullptr)) {
                    if (jsonData["code"].asInt() == 200) {
                        bRet = true;
                        std::stringstream httpData(jsonData["results"].toStyledString());
                        if (Json::parseFromStream(jsonReader, httpData, &jsonData, nullptr)) {
                            pdev->rtsp_addr = jsonData["url"].asString();
                        }
                    }
                }
            }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);
    }

    return bRet;
}

bool CVurixDevice::KeepAlive()
{
    bool bRet = false;

    std::unique_lock<std::mutex> lock(_mtx_p);

    const std::string url = _http_url + ":" + std::to_string(_port) + "/api/keep-alive";
    CURL* curl = curl_easy_init();

    if (curl) {
        struct curl_slist* chunk = nullptr;

        std::string header = "Accept: application/json";
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-auth-token: " + _auth_token;
        chunk = curl_slist_append(chunk, header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        long httpCode(0);
        std::stringstream root;

        // Hook up data handling function.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CVurixDevice::callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &root);

        // Run our HTTP GET command, capture the HTTP response code, and clean up.
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (httpCode == 200) {
                bRet = true;
             }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);
    }

    return bRet;
}

void CVurixDevice::GetEvents()
{
    const std::string url = _http_url + ":" + std::to_string(_port) + "/api/event/receive?system=true";

    std::unique_lock<std::mutex> lock(_mtx_p);

    if (_curl_evt == nullptr)
        _curl_evt = curl_easy_init();

    if (_curl_evt) {
        _api_serial++;

        struct curl_slist* chunk = nullptr;

        std::string header = "x-auth-token: " + _auth_token;
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-api-serial: " + std::to_string(_api_serial);
        chunk = curl_slist_append(chunk, header.c_str());
 
        curl_easy_setopt(_curl_evt, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(_curl_evt, CURLOPT_URL, url.c_str());
        //curl_easy_setopt(_curl_evt, CURLOPT_VERBOSE, 1L);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(_curl_evt, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        //curl_easy_setopt(_curl_evt, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(_curl_evt, CURLOPT_FOLLOWLOCATION, 1L);

        long httpCode(0);
        std::stringstream root;

        // Hook up data handling function.
        curl_easy_setopt(_curl_evt, CURLOPT_WRITEFUNCTION, CVurixDevice::callbackevent);
 
        // Run our HTTP GET command, capture the HTTP response code, and clean up.
        curl_easy_perform(_curl_evt);

//       curl_easy_cleanup(_curl_evt);
        curl_slist_free_all(chunk);
    }
}

void CVurixDevice::KeepAliveThread()
{
    while (_loop) {
        if (!KeepAlive()) {
            _loop = false;
            break;
        }
        std::unique_lock<std::mutex> lock(_mtx);
        _cond.wait_for(lock, std::chrono::minutes(5));
    }
}

bool CVurixDevice::GetRtspAddr(std::string dev_serial, std::string& address) const
{
    bool bRet = false;

    for (uint32_t i = 0; i < _dev_count; i++) {
        if (_devices[i].serial == dev_serial) {
            address = _devices[i].rtsp_addr;
            bRet = true;
            break;
        }
    }

    return bRet;
}

bool CVurixDevice::GetRtspAddr(std::string dev_serial, std::string& address, std::string& host, uint16_t& port, std::string& uri) const
{
    bool bRet = false;

    for (uint32_t i = 0; i < _dev_count; i++) {
        if (_devices[i].serial == dev_serial) {
            address = _devices[i].rtsp_addr;
            bRet = true;
            break;
        }
    }

    if (bRet) {
        std::string full_addr;

        int index = address.find("//");
        if (index != std::string::npos)
            index += 2;
        std::string str2 = address.substr(index, address.length() - index);

        index = str2.find("/");
        if (index != std::string::npos) {
            full_addr = str2.substr(0, index);
            uri = str2.substr(index + 1, str2.length() - index - 1);
        }
        else {
            full_addr = str2;
        }

        if (full_addr.at(0) == '[') {
            index = full_addr.rfind("]");
            if (index != full_addr.length() - 1) {
                index = full_addr.rfind(":");
                std::string tmp_addr = full_addr.substr(0, index);
                std::string tmp_port = full_addr.substr(index + 1, full_addr.length() - index - 1);

                if (tmp_addr.at(0) == '[' && tmp_addr.at(tmp_addr.length() - 1) == ']')
                    tmp_addr = tmp_addr.substr(1, tmp_addr.length() - 2);

                host = tmp_addr;
                port = atoi(tmp_port.c_str());
            }
            else {
                host = full_addr.substr(1, full_addr.length() - 2);
            }
        }
        else {
            index = full_addr.rfind(":");
            if (index != std::string::npos) {
                host = full_addr.substr(0, index);
                std::string tmp_port = full_addr.substr(index + 1, full_addr.length() - index - 1);
                port = atoi(tmp_port.c_str());
            }
            else {
                host = full_addr;
            }
        }
    }

    return bRet;
}

bool CVurixDevice::SetReferenceDevice(std::string dev_serial)
{
    _ref_device_list.push_back(dev_serial);

    return true;
}

void CVurixDevice::SendEvent(string ipaddr, uint16_t port, std::string dev_serial, int algId, SYSTEMTIME Time)
{
    Json::Value root;

    if (algId == 1) {
        root["event_id"] = 69;
        root["event_msg"] = "Intrusion Event is occurred";
    }
    else if (algId == 2) {
        root["event_id"] = 55;
        root["event_msg"] = "Loitering Event is occurred";
    }
    else if (algId == 12) {
        root["event_id"] = 208;
        root["event_msg"] = "Falldown Event is occurred";
    }
    else if (algId == 14) {
        root["event_id"] = 67;
        root["event_msg"] = "Smoke Event is occurred";
    }
    else if (algId == 6) {
        root["event_id"] = 68;
        root["event_msg"] = "Fire Event is occurred";
    }
    else {
        return;
    }

    root["dev_serial"] = dev_serial;
    root["dch_ch"] = 0;
    root["event_status"] = 1;
    char buf[64] = { 0, };
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds);
    root["event_time"] = buf;

    Json::StreamWriterBuilder builder;
    const std::string output = Json::writeString(builder, root);

    string http_url = "http://" + ipaddr;
    const string url = http_url + ":" + to_string(port) + "/api/event/send-vca";

    std::unique_lock<std::mutex> lock(_mtx_p);

    CURL* curl = curl_easy_init();
    if (curl) {
        _api_serial++;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        struct curl_slist* chunk = nullptr;
        chunk = curl_slist_append(chunk, "Content-Type: application/json");

        string header = "x-auth-token: " + _auth_token;
        chunk = curl_slist_append(chunk, header.c_str());
        header = "x-api-serial: " + to_string(_api_serial);
        chunk = curl_slist_append(chunk, header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);

        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, output.c_str());

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}