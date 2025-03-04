#pragma once

#include "resource.h"
#include "DataBaseManager.h"
#include "Inifile.h"
#include "GroupChannelManager.h"
#include "DeviceStream.h"
#include "InferenceStream.h"
#include "TCPServer.h"

#include <thread>
#include <json/json.h> 

typedef struct ConnectInfo {
	std::vector<ChInfo_t> Cameras;
	std::vector<int> evtDurTimes;
	AlgOpt_t algOpt;
} ConnectInfo_t;

class IoS : public ITCPServerListener
{
public:
	IoS();
	~IoS();

public:
	bool startup(int port, int port_mas, int port_link);
	void shutdown();

public:
	const ConnectInfo_t& GetConnectInfo() const { return _ConnectInfo; }

private:
	TCPServer* _server = nullptr;			//9001 evt서버와 통신
	TCPServer* _server_mas = nullptr;		//9000 master-slave 통신, Master에서 Slave가 죽어있는지 감시(부가기능), Slave가 어느 R2에 붙을지 요청(기본기능)
	TCPServer* _server_link = nullptr;		//port : 9006 - Master가 죽어있는지 감시

private:
	// ITCPServerListener
	virtual void onServerSessionOpened(TCPSession* session) override;
	virtual void onServerSessionClosed(TCPSession* session) override;
	virtual void onRecvData(TCPSession* session, const uint8_t *data, int size) override;

	bool GetRtspAddr(string& address, string& host, uint16_t& port, string& uri) const;
	void StartLBClient();
	void StopLBClient();
	void LBClientThread(int index);
	void UpdateClientConnection(int index);
	void RestartClientConnection(int index);
	void ConnectLogThread();
	void StartConnectLogThread();
	void StopConnectLogThread();

	void StartSystem(std::string evtIP, std::string curIP);
	void StopAnalyses();
	void StartAlgAll();
	void StopAlgAll();
	void ApplyAlgAll();
	void ApplyAlgCamOpt(int ch_seq);
	void StartAlgCamera(std::string camList);
	void StopAlgCamera(std::string camList);
	void AddCamera(std::string camList, int mode = 0);
	void RemoveCamera(std::string camList, int mode = 0);
	bool GetCameraInfo(int ch_seq);
	void EnableCameraState();
	uint32_t ParseAlgMask(CString sTok);
	bool GetRoiInfo(int ch_seq);
	bool GetRoiInfo(int ch_seq, RoiInfo_t roiInfo[]);
	std::vector<CPoint> GetRoiPtInfo(int roi_seq);
	bool GetCamOptInfo(int ch_seq);
	bool GetCamOptInfo(int ch_seq, AlgOpt_t Opt[]);
	void ResetProcess();
	void UpdateProcess(int index, int value); 

private:
	std::thread _th_conn;
	bool _bLoopConn = false;

	std::thread _th_ptz;
	bool _bLoopPTZ = false;

	CIniFileW  _iniFile;
	CString _iniFileName = L"DB.ini";
	DataBaseManager* _pDBManager = nullptr;
	ConnectInfo_t _ConnectInfo;
	std::vector<std::vector<DeviceStream*>> _groupStreams;		//dnn그룹, dnn index 리스트, _groupStreams[a][b] a= gpu의 dnn 인덱스 , b = dnn의 channel 인덱스
	GroupChannelManager _grpChMgr;
	std::vector<std::vector<InferenceStream*>> _inferenceStreams;

	std::vector<std::thread> _lbths;
	std::vector<bool> _bLoopLBs;

	std::thread _th_link;
	bool _bLink = false;

	ChInfo_t _Camera;
	std::vector<TCPClient> _cliAIs;

	std::vector<SOCKET> _masAISocks;
	std::vector<bool> _bMasAIConnecteds;
	std::vector<bool> _bEnableAIs;
	std::mutex _mtxMaster;

	CDBPostgre* _pPTZDBPostgre = nullptr;

	CDBPostgre* _pEvtDBPostgre = nullptr;
	EventManager* _pEvtMgr = nullptr;

	std::mutex _mtx;
	std::mutex _mtxMsg;

	std::thread _th_check;
	bool _bLoopCheck = false;

private:
	void LoadInitData();
	void StartRecovery1();
	void StartRecovery2();
	bool InitAI();
	bool InitMasterSlave();
	int InitCamAlgMode(int ch_seq);
	bool InitCamAlgModeEtc(int ch_seq, ChInfo_t& ChInfo);
	int InitEventDurTime(std::vector<int>& durTimes);
	int InitCamEventDurTime(int ch_seq, vector<int>& durTimes);
	void StopAnalysis(int group_index);
	void StartAnalysisAll();
	void StopAnalysisAll();
	void StartPTZLockThread();
	void StopPTZLockThread();
	void PTZLockThread();
	std::vector<int> GetLockLists() const;

	void StartLinkThread();
	void StopLinkThread();
	void LinkThread();
	bool ChangeMaster(int index);
	bool Connect(SOCKET sock, char* host, int port, int timeout);
	void GenerateStillImage(int ch_seq);
	bool GetIoSName();
	bool UpdateAIState(int index, bool bState); //kimdh0721 - R2 고장처리
	bool UpdateIOSState(string IoSIP, bool bState);
	bool UpdateErrorMgr(string CurIP);
	bool IoSsetcheck(string IoSIP);
	void StartConCheckThread();
	void StopConCheckThread();
	void ConCheckThread();

};
