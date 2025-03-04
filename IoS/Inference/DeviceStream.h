#pragma once

#include "pch.h"
#include <map>
#include <thread>
#include <mutex>
#include <cstring>
#include <opencv2/opencv.hpp>

#include "define.h"
#include "RTSPClient.h"
#include "StreamDataQ.h"
#include "GroupChannelManager.h"
#include "InferenceStream.h"
#include "TCPClient.h"
#include "EventManager.h"

extern "C" {
#include <libavutil/opt.h>        
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")
}

#pragma warning(disable:4018)
#pragma warning(disable:4819)
#pragma warning(disable:4996)

#define	MAX_CONS_PER_AI				80

using namespace cv;

struct VideoHeader {
	int			camera_seq;
	int			codec_type;
	int			frame_type;
	SYSTEMTIME	st;
	bool		bDetected;
	uint32_t	buf_size;
};

struct VideoInfo {
	VideoHeader header;
	uint8_t* buf;
};

class DeviceStream
{
public:
	DeviceStream(std::vector<InferenceStream*> pInfStreams, GroupChannelManager* mgr, std::string evtIP, std::string curIP, EventManager* evtMgr);
	~DeviceStream();

public:
	void setVideoStream(VideoStreamData* pStream);
	void StreamThread();
	void TCPSendThread();

private:
	void pushFrame(Mat &frame);
	bool ConnectToAI(int& aidx);
	void DisconnectAI();
	void SetTransferCount(int count);

public:
	bool Connect(std::string host, uint16_t rtsp_port, std::string uri, std::string id, std::string pass);
	int	channelSeq() { return _ch_seq; }
	void SetPTZLock(bool bLock) { _bPTZLock = bLock; }

private:
	bool Connect();
	void Disconnect();

private:
	ChInfo_t		_CamInfo, _CamInfo_copy;
	CString			_strChannelName;

	bool			_bLoop;
	std::thread		_th;
	std::thread		_thSnd;
	std::mutex		_mtxSnd;
	std::condition_variable _condSnd;
	std::list<uint8_t*> _listSnd;
	std::list<uint8_t*> _listTcp;


	StreamDataQ	_streamDataQ;
	std::string _hostName;
	uint16_t _rtsp_port;
	std::string _userID;
	std::string _password;
	std::string _uri;

	int _streamInCnt = 0;
	int _conn_state = 0;
	bool _signal_state = true;
	int nalunit_first=0;

	int _use_ptz = 0;

	int _ns_count = 0;					//영상 fail 나면 count 증가, NoSignal
	int _retry_connect = 0;
	

	RTSPClient* _pRtspClient = nullptr;

	AVCodec* _pCodec = nullptr;
	AVCodecContext* _pCodecCtx = nullptr;
	int _preWidth = -1;
	int _preHeight = -1;
	int _preCodec = -1;
	bool _bReadyCodec = false;

	int _ch_seq = -1;
	std::string _ch_id;
	int _group_Index = -1;

	bool _bEvent = false;

	std::vector<InferenceStream*> _pInfStreams;
	GroupChannelManager* _pGrpMgr = nullptr;

	SOCKET _sndSock = INVALID_SOCKET;
	bool _bConnected = false;
	int _IndexAI = -1;
	int _TransCount = 2;	// 4;
	std::chrono::time_point<std::chrono::steady_clock> _evt_start;
	std::chrono::time_point<std::chrono::steady_clock> _frame_tp;
	std::chrono::time_point<std::chrono::steady_clock> _trans_tp;
	bool _bPTZLock = false;

	std::string _evtIP;
	std::string _curIP;

	EventManager* _pEvtMgr = nullptr;

	bool _bFirstFrame = true;
	bool _bIdleState = true;
	bool _bChangeInfo = false;


private:
	static void frameHandlerFunc(void* arg, RTP_FRAME_TYPE frame_type, int64_t timestamp, unsigned char* buf, int len);
	bool CreateCodec(int codecType);
	void DeleteCodec();
	void PushSendData(uint8_t* pData);
	uint8_t* MakeSendData(VideoStreamData* pStream, bool bDetected);
	bool UpdateAIState(int index, bool bState);
	BOOL FindDir(CString sDir);
	void CreateFolder(CString sPath);
	bool SendEvent(std::string ch_id, int ch_seq, SYSTEMTIME Time, string ipaddr, Mat& frame);
	bool UpdateCameraState(int state);
	bool UpdateErrorMgr();

public:
	unsigned int _volSize = 0;
	uint8_t* _pVolData = nullptr;				//sps, pps 정보 포인터
	bool retry_voldata = false;

public:
	void ResetCameraState();
	void DisconnectAll();
	void StopThreads();
	void SetCamInfo(ChInfo_t camInfo);
	bool IsIdleState() const;

private:
	bool Connect(SOCKET sock, char* host, int port, int timeout);
};
