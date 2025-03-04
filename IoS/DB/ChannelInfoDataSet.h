#pragma once

#include <cmath>
#include <iostream>
#include <string>

class ChannelInfoDataSet
{
public:
	ChannelInfoDataSet() {};
	~ChannelInfoDataSet() {};

	//variable
private:
	CString ch_name;
	std::string ch_id;
	int ch_seq = 0;
	std::string login_id;
	std::string login_pw;
	std::string rtsp_address;
	std::string rtsp_address2;
	bool support_ptz = false;
	int video_type = 1;
	int err_status = 0;

	//setter
public:
	void setCamera_name(CString var) { ch_name = var; }
	void setCamera_id(std::string var) { ch_id = var; }
	void setCamera_seq(int var) { ch_seq = var; }
	void setLogin_id(std::string var) { login_id = var; }
	void setLogin_pw(std::string var) { login_pw = var; }
	void setRtsp_address(int index, std::string var) { (index == 0) ? rtsp_address = var : rtsp_address2 = var; }
	void setSupport_ptz(std::string var) { support_ptz = (var == "true") ? true : false; }
	void setVideo_type(int var) { video_type = var; }
	void setErr_status(int var) { err_status = var; }


	//getter
public:
	CString getCamera_name() { return ch_name; }
	std::string getCamera_id() { return ch_id; }
	int getCamera_seq() { return ch_seq; }
	std::string getLogin_id() { return login_id; }
	std::string getLogin_pw() { return login_pw; }
	std::string getRtsp_address(int index) { std::string address; (index == 0) ? address = rtsp_address : address = rtsp_address2; return address; }
	bool getSupport_ptz() { return support_ptz; }
	int getVideo_type() { return video_type; }
	int getErr_status() { return err_status; }
};