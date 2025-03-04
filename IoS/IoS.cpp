#include "pch.h"
#include "framework.h"
#include <winsock2.h>
#include <tlhelp32.h>
#include "IoS.h"
#include "KISA_SEED_CBC.h"
#include "Base64.h"
#include <libavutil/log.h>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifndef _DEBUG
//#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif

// 🟢 전역 변수 정의
std::mutex g_CoordMtx;
std::queue<ObjectCoord> g_CoordQueue;
std::condition_variable g_CoordCV;

struct CmdHeader {
	uint8_t		cmd;
	uint8_t		type;
	uint8_t		reply;
	uint8_t		data_size;
};

// 유일한 애플리케이션 개체입니다.

CWinApp theApp;

using namespace std;

bool g_bStartSystem = false;
bool g_bAlgorithm = false;

std::string _evtIP;
std::string _curIP;
int							g_nIoSInx = -1;
int							g_nMasterInx = -1, g_nPreMasterInx = -1;
bool						g_bMaster = false;
std::vector<std::string>	g_IoSLists;
std::vector<CString> g_IoSNames;
std::map<std::pair<std::string, int>, uint8_t> g_IoSCons;
std::mutex					g_IoSMtx;

std::vector<uint8_t>	g_AICons;		//R2별 connect channel 수(send) - 선별데이터를 R2에 보낼때 IOS(M)가 가지고 있는 변화되는 R2 conch table
std::vector<uint8_t>	g_AIRecvCons;	//R2별 connect channel 수(recv) - R2가 자신의 conch의 수를 전송하면 받는 table
										//Ios(M)가 데이터를 R2에게 보내고 ch수를 받을때의 딜레이로 인해 나눠놓았음
std::mutex				g_AIMtx;
std::vector<std::string> g_AIIPs;		//R2별 IP저장 table
std::vector<CString> g_AINames;

CString g_strIoSName;
CString g_strIoSSeq;

std::mutex	g_MtxConn;
int g_nConnect = 0;

#if _DBINI
std::string _DBname;
#endif

IoS::IoS()
{
	TCHAR szPath[MAX_PATH];
	::GetModuleFileName(NULL, szPath, MAX_PATH);

	CString strPath = szPath;
	if (0 < strPath.ReverseFind('\\')) {
		strPath = strPath.Left(strPath.ReverseFind('\\'));
	}

	::SetCurrentDirectory(strPath);

	if (GetFileAttributes(FILE_EVENT_PATH) == -1)
		CreateDirectory(FILE_EVENT_PATH, NULL);

	LoadInitData();		//DB.ini 파일 읽어서 evtip, curip 불러오기
	GetIoSName();		//IOS 이름 가져오기

	if (_evtIP.empty() || _curIP.empty())
		return;

	_groupStreams.resize(MAX_DNN_COUNT);				//dnn 그룹 사이즈 설정
	_ConnectInfo.evtDurTimes.reserve(MAX_ALG_COUNT);	//evt duration time 공간을 알고리즘 갯수만큼 미리 공간만 할당

	std::vector<InferenceStream*> inferenceStream;		//inferenceStrean - 모델(obj, fire)
	for (int i = 0; i < MAX_DNN_COUNT; i++) {
		inferenceStream.clear();
		for (int j = 0; j < MAX_INF_MODEL_COUNT; j++) {
			InferenceStream* pInfStream = new InferenceStream(i, j);
			inferenceStream.push_back(pInfStream);
		}
		_inferenceStreams.push_back(inferenceStream);
	}

	_server_link = new TCPServer(this);		//port : 9006 - Master가 죽어있는지 감시
	cout << "port : 9006" << endl;
	_server_mas = new TCPServer(this);		//9000 master-slave 통신
	cout << "port : 9000" << endl;
	_server = new TCPServer(this);			//9001 evt서버와 통신
	cout << "port : 9001" << endl;

	InitAI();								//R2의 IP와 상태 읽어오기
	
	InitMasterSlave();						//Ios의 IP와 Master가 누구인지(Master상태) 읽어오기
	
	StartLBClient();						//R2들과 연결, R2에게서 받는데이터나 연결상태로 R2가 살아있는지 죽었는지 알수있음	- Thread
	
#ifdef _DIRECT_DB
	_pEvtDBPostgre = new CDBPostgre(_evtIP);
	if (_pEvtDBPostgre) {
		if (!_pEvtDBPostgre->_pDB) {
			delete _pEvtDBPostgre;
			_pEvtDBPostgre = nullptr;
		}
		else {
			ConnStatusType connstatus = PQstatus(_pEvtDBPostgre->_pDB->m_conn);
			if (connstatus == CONNECTION_BAD) {
				_pEvtDBPostgre->_pDB->CloseConnection();
				delete _pEvtDBPostgre;
				_pEvtDBPostgre = nullptr;
			}
		}
	}
#endif

	_pEvtMgr = new EventManager(_pEvtDBPostgre, _evtIP);

	for (int i = 0; i < MAX_CAMERA_COUNT; i++) {		//dnn index 순서대로 channel 할당 (channel 할당하면서 영상 decoding thread, send thread 실행)
		int gdx = i % MAX_DNN_COUNT;
		DeviceStream* pStream = new DeviceStream(_inferenceStreams[gdx], &_grpChMgr, _evtIP, _curIP, _pEvtMgr);
		_groupStreams[gdx].push_back(pStream);
	}

	_pDBManager = new DataBaseManager(_evtIP, _curIP);
	if (_pDBManager) {
		if (!_pDBManager->CreateDBprocess()) {
			delete _pDBManager;
			_pDBManager = nullptr;
		}
	}

	if (_pDBManager) {
		if (_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getSend_curation() == 1) {
			StartRecovery1();
			if (_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getSend_alg() == 1) {
				StartRecovery2();
			}

			int value1 = _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getSend_config();
			int value2 = _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getOp_config();

			if (value1 < 0 || value1 > 2) {
				UpdateProcess(4, 0);
				UpdateProcess(5, 0);
			}
			else if (value1 != value2) {
				UpdateProcess(5, -1);
			}
		}
		else {
			UpdateProcess(1, 2);
			ResetProcess();

			_ConnectInfo.Cameras.clear();

			if (_pDBManager) {
				delete _pDBManager;
				_pDBManager = nullptr;
			}
		}
	}
#if 0
	_pPTZDBPostgre = new CDBPostgre(_evtIP);		//DB에 접속
	if (_pPTZDBPostgre) {
		if (!_pPTZDBPostgre->_pDB) {
			delete _pPTZDBPostgre;
			_pPTZDBPostgre = nullptr;
		}
		else {
			ConnStatusType connstatus = PQstatus(_pPTZDBPostgre->_pDB->m_conn);		//접속 실패시
			if (connstatus == CONNECTION_BAD) {
				_pPTZDBPostgre->_pDB->CloseConnection();
				delete _pPTZDBPostgre;
				_pPTZDBPostgre = nullptr;
			}

			StartPTZLockThread();		//각 카메라의 PTZ상태 읽어오기 - 지속 Thread
		}
	}
#endif
	StartPTZLockThread();

	StartLinkThread();		//IOS끼리 연결 - 본인이 Master, Slave인지 설정

	StartConCheckThread();
	UpdateErrorMgr(_curIP);
//	StartConnectLogThread();
}

IoS::~IoS()
{
	StopConnectLogThread();
	StopPTZLockThread();
	StopConCheckThread();

	for (int gdx = 0; gdx < MAX_DNN_COUNT; gdx++) {
		for (int i = 0; i < _groupStreams[gdx].size(); i++) {
			DeviceStream* pStream = _groupStreams[gdx][i];
			if (pStream)
				delete pStream;
		}
		_groupStreams[gdx].clear();
	}

	if (_pEvtMgr) {
		delete _pEvtMgr;
		_pEvtMgr = nullptr;
	}

	if (_pDBManager) {
		delete _pDBManager;
		_pDBManager = nullptr;
	}

	for (int i = 0; i < MAX_DNN_COUNT; i++) {
		for (int j = 0; j < MAX_INF_MODEL_COUNT; j++) {
			InferenceStream* pInfStream = _inferenceStreams[i][j];
			if (pInfStream) {
				delete pInfStream;
				pInfStream = nullptr;
			}
		}
	}
	_inferenceStreams.clear();

	if (_pEvtDBPostgre) {
		_pEvtDBPostgre->_pDB->CloseConnection();
		delete _pEvtDBPostgre;
		_pEvtDBPostgre = nullptr;
	}

	if (_pPTZDBPostgre) {
		_pPTZDBPostgre->_pDB->CloseConnection();
		delete _pPTZDBPostgre;
		_pPTZDBPostgre = nullptr;
	}
	
	if (_server) {
		delete _server;
        _server = nullptr;
    }

	if (_server_mas) {
		delete _server_mas;
		_server_mas = nullptr;
	}

	if (_server_link) {
		delete _server_link;
		_server_link = nullptr;
	}

	for (int i = 0; _masAISocks.size(); i++) {
		if (_masAISocks[i] != INVALID_SOCKET) {
			closesocket(_masAISocks[i]);
			_masAISocks[i] = INVALID_SOCKET;
		}

		_bMasAIConnecteds[i] = false;
		_bEnableAIs[i] = false;
	}
}

bool IoS::GetRtspAddr(string& address, string& host, uint16_t& port, string& uri) const
{
	string full_addr;

	port = 554;
	uri = "";

	int index = address.find("//");
	if (index != string::npos)
		index += 2;
	else
		return false;

	string str2 = address.substr(index, address.length() - index);

	index = str2.find("/");
	if (index != string::npos) {
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
			string tmp_addr = full_addr.substr(0, index);
			string tmp_port = full_addr.substr(index + 1, full_addr.length() - index - 1);

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
		if (index != string::npos) {
			host = full_addr.substr(0, index);
			string tmp_port = full_addr.substr(index + 1, full_addr.length() - index - 1);
			port = atoi(tmp_port.c_str());
		}
		else {
			host = full_addr;
		}
	}

	return true;
}

void IoS::StopAnalysis(int group_index)
{
	if (group_index < 0 || group_index >= MAX_DNN_COUNT)
		return;

	std::unique_lock<std::mutex> lock(_mtx);
	for (int i = 0; i < _groupStreams[group_index].size(); i++) {
		DeviceStream* pStream = _groupStreams[group_index][i];
		if (pStream && pStream->IsIdleState() == false) {
			pStream->DisconnectAll();
//			pStream->StopThreads();
		}
	}
}


void IoS::StartAnalysisAll()
{
	std::string host, uri;
	uint16_t rtsp_port;

	for (int i = 0; i < _ConnectInfo.Cameras.size(); i++) {
		int gdx = i % MAX_DNN_COUNT;
		DeviceStream* pStream = _groupStreams[gdx][i / MAX_DNN_COUNT];
		if (pStream) {
			pStream->SetCamInfo(_ConnectInfo.Cameras[i]);
			//0502 - kdh first_rtsp
			std::string addr = _ConnectInfo.Cameras[i].rtsp_address[1].size() > 0 ? _ConnectInfo.Cameras[i].rtsp_address[1] : _ConnectInfo.Cameras[i].rtsp_address[0];
			//std::string addr = _ConnectInfo.Cameras[i].rtsp_address[1].size() > 0 ? _ConnectInfo.Cameras[i].rtsp_address[0] : _ConnectInfo.Cameras[i].rtsp_address[0];
			if (GetRtspAddr(addr, host, rtsp_port, uri)) {
				pStream->Connect(host, rtsp_port, uri, _ConnectInfo.Cameras[i].login_id, _ConnectInfo.Cameras[i].login_pass);
			}
		}
	}
}


void IoS::StopAnalysisAll()
{
	for (int i = 0; i < MAX_DNN_COUNT; i++) {
		StopAnalysis(i);
//		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void IoS::StartLBClient()
{
	for (int i = 0; i < g_AIIPs.size(); i++) {
		_bLoopLBs[i] = true;
		_lbths[i] = std::thread{ &IoS::LBClientThread, this, i };
	}
}

void IoS::StopLBClient()
{
	for (int i = 0; i < g_AIIPs.size(); i++) {
		if (_bLoopLBs[i]) {
			_bLoopLBs[i] = false;
			_lbths[i].join();
		}
	}
}

void IoS::LBClientThread(int index)		//port : 9003  R2연결, R2가 죽었나 살았나만 확인하는 Thread
{
	int ret;
	bool bConnected = false;
	DWORD recvTimeout = 10000;
	SOCKET sock = INVALID_SOCKET;
	
	WSADATA wsData;
	int wsResult = WSAStartup(MAKEWORD(2, 2), &wsData);
	if (wsResult != 0) {
//		std::cerr << "Can't start Winsock, Err #" << wsResult << std::endl;
		_bLoopLBs[index] = false;
		return;
	}

	while (_bLoopLBs[index]) {
		std::unique_lock<std::mutex> lock(g_IoSMtx);
		bool bMaster = g_bMaster;
		lock.unlock();
		
		if (bMaster) {
			if (index < g_AICons.size() - 1) {
				_mtxMaster.lock();
				RestartClientConnection(index);
				_mtxMaster.unlock();
				if (_bEnableAIs[index] == false) continue;		//kimdh0721 - r2 down,reboot error
			}
		}

		if (sock == INVALID_SOCKET) {
			sock = socket(AF_INET, SOCK_STREAM, 0);
			//setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(recvTimeout));		//kimdh0716 - r2 down,reboot error
			if (sock == INVALID_SOCKET) {
//				std::cerr << "Can't create socket, Err #" << WSAGetLastError() << std::endl;
				_bLoopLBs[index] = false;
				WSACleanup();
				return;
			}
		}

		if (bConnected == false) {
			if (Connect(sock, (char*)g_AIIPs[index].c_str(), 9003, 1) == false) {
//				std::cerr << "Can't connect to server, Err #" << WSAGetLastError() << std::endl;
				closesocket(sock);
				sock = INVALID_SOCKET;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			bConnected = true;
		}

		TTCPPacketHeader header;
		ret = recv(sock, (char*)&header, sizeof(TTCPPacketHeader), 0);
		if (ret != sizeof(TTCPPacketHeader)) {		//Header를 못 받았을때
			g_AIMtx.lock();
			g_AIRecvCons[index] = 255;
			std::cout << "recv1[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << ", 255" << std::endl;
			if (g_AICons[index] != 255) {
				g_AICons[index] = 255;
				if (index < g_AICons.size() - 1) {
					for (int i = 0; i < g_IoSLists.size(); i++)
						g_IoSCons[make_pair(g_IoSLists[i], index)] = 0;
				}
//				std::cout << "recv[" << std::to_string(index) << "] = " << "255" << std::endl;
			}
			g_AIMtx.unlock();
			closesocket(sock);
			sock = INVALID_SOCKET;
			bConnected = false;

			_mtxMaster.lock();
			if (_masAISocks[index] != INVALID_SOCKET) {
				closesocket(_masAISocks[index]);
				_masAISocks[index] = INVALID_SOCKET;
			}

			_bMasAIConnecteds[index] = false;
			_bEnableAIs[index] = false;
			_mtxMaster.unlock();

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			//UpdateAIState(index, false);	//kimdh0721 - R2 고장관리
			continue;
		}

		if (header.code[0] != 'T' || header.code[1] != 'C' || header.code[2] != 'R' || header.code[3] != 'S') {
			closesocket(sock);
			sock = INVALID_SOCKET;
			bConnected = false;
			_mtxMaster.lock();
			if (_masAISocks[index] != INVALID_SOCKET) {
				closesocket(_masAISocks[index]);
				_masAISocks[index] = INVALID_SOCKET;
			}
			_bMasAIConnecteds[index] = false;
			_bEnableAIs[index] = false;
			_mtxMaster.unlock();

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			//UpdateAIState(index, false);	//kimdh0721 - R2 고장관리
			continue;
		}

		if (header.contentLength != 1) {		//recv값이 이상한 값일때( 정해진 규정 recv값이 아닐떄)
			g_AIMtx.lock();
			g_AIRecvCons[index] = 255;
			std::cout << "recv2[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << ", 255" << std::endl;
			if (g_AICons[index] != 255) {
				g_AICons[index] = 255;
				if (index < g_AICons.size() - 1) {
					for (int i = 0; i < g_IoSLists.size(); i++)
						g_IoSCons[make_pair(g_IoSLists[i], index)] = 0;
				}
//				std::cout << "recv[" << std::to_string(index) << "] = " << "255" << std::endl;
			}
			g_AIMtx.unlock();
			closesocket(sock);
			sock = INVALID_SOCKET;
			bConnected = false;

			_mtxMaster.lock();
			if (_masAISocks[index] != INVALID_SOCKET) {
				closesocket(_masAISocks[index]);
				_masAISocks[index] = INVALID_SOCKET;
			}

			_bMasAIConnecteds[index] = false;
			_bEnableAIs[index] = false;
			_mtxMaster.unlock();

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			//UpdateAIState(index, false);	//kimdh0721 - R2 고장관리
			continue;
		}

		uint8_t data;
		ret = recv(sock, (char*)&data, 1, 0);
		if (ret != 1) {
			g_AIMtx.lock();
			g_AIRecvCons[index] = 255;
			std::cout << "recv3[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << ", 255" << std::endl;
			if (g_AICons[index] != 255) {
				g_AICons[index] = 255;
				if (index < g_AICons.size() - 1) {
					for (int i = 0; i < g_IoSLists.size(); i++)
						g_IoSCons[make_pair(g_IoSLists[i], index)] = 0;
				}
//				std::cout << "recv[" << std::to_string(index) << "] = " << "255" << std::endl;
			}
			g_AIMtx.unlock();
			closesocket(sock);
			sock = INVALID_SOCKET;
			bConnected = false;

			_mtxMaster.lock();
			if (_masAISocks[index] != INVALID_SOCKET) {
				closesocket(_masAISocks[index]);
				_masAISocks[index] = INVALID_SOCKET;
			}

			_bMasAIConnecteds[index] = false;
			_bEnableAIs[index] = false;
			_mtxMaster.unlock();

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			//UpdateAIState(index, false);	//kimdh0721 - R2 고장관리

			continue;
		}

		g_AIMtx.lock();
		g_AIRecvCons[index] = data;
		std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << ", " << std::to_string(data) << std::endl;
//		std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(data) << std::endl;
#if 0
		if (bMaster == false) {
			g_AICons[index] = data;
			std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(data) << std::endl;
		}
		else {
			if (g_AICons[index] == 255) {
				g_AICons[index] = data;
				std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(data) << std::endl;
			}
		}
#else
#if 0
		if (bMaster == false) {
			if (index == g_AICons.size() - 1) {
				g_AICons[index] = data;
				std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(data) << std::endl;
			}
		}
		else {
			if (g_AICons[index] == 255) {
				g_AICons[index] = data;
				std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(data) << std::endl;
			}
		}
#else
		if (bMaster == false) {
			if (index == g_AICons.size() - 1) {
				g_AICons[index] = data;
//				std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(data) << std::endl;
			}
/*
			else if (g_AICons[index] == 255) {
				g_AICons[index] = data;
			}
*/
		}
		else {
			if (g_AICons[index] == 255) {
				g_AICons[index] = data;
//				std::cout << "recv[" << std::to_string(index) << "] = " << std::to_string(data) << std::endl;
			}
		}
#endif
#endif
		g_AIMtx.unlock();

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}

	WSACleanup();
}

void IoS::UpdateClientConnection(int index)
{
	if (index < 0 || index >= g_AIIPs.size() - 1)
		return;

	if (_masAISocks[index] == INVALID_SOCKET) {
		SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET)
			return;
		_masAISocks[index] = sock;
	}

	if (_bMasAIConnecteds[index] == false) {
		bool bConnected = false;
		bool tmp;
		if (tmp = Connect(_masAISocks[index], (char*)g_AIIPs[index].c_str(), 9005, 10))
			bConnected = true;

		if (bConnected == false)
			return;

		_bMasAIConnecteds[index] = true;
	}

	TTCPPacketHeader header;
	::memcpy(&header.code, TCPSessionParser::PACKET_MAGIC_CODE, 4);
	header.contentLength = 1;

	if (::send(_masAISocks[index], (const char*)&header, sizeof(header), 0) != sizeof(header))
		return;

	uint8_t data = 0;
	if (::send(_masAISocks[index], (const char*)&data, 1, 0) != 1)
		return;

	TTCPPacketHeader ReplyHeader;
	int ret = recv(_masAISocks[index], (char*)&ReplyHeader, sizeof(TTCPPacketHeader), 0);
	if (ret != sizeof(TTCPPacketHeader))
		return;

	if (ReplyHeader.code[0] != 'T' || ReplyHeader.code[1] != 'C' || ReplyHeader.code[2] != 'R' || ReplyHeader.code[3] != 'S')
		return;

	if (ReplyHeader.contentLength < 17 || ReplyHeader.contentLength % 17 != 0)
		return;

	uint8_t* buf = new uint8_t[ReplyHeader.contentLength];
	if (buf == nullptr)
		return;

	::memset(buf, 0x00, sizeof(ReplyHeader.contentLength));
	ret = recv(_masAISocks[index], (char*)buf, ReplyHeader.contentLength, 0);
	if (ret != ReplyHeader.contentLength) {
		delete[] buf;
		return;
	}

	int len = ReplyHeader.contentLength / 17;
	for (int i = 0; i < len; i++) {
		std::string ipaddr = (char*)&buf[i * 17];
		g_IoSCons[make_pair(ipaddr, index)] = buf[(i + 1) * 17 - 1];
		std::cout << ipaddr << "[" << std::to_string(index) << "] = " << std::to_string(g_IoSCons[make_pair(ipaddr, index)]) << std::endl;
	}

	if (g_nMasterInx == g_nPreMasterInx) {
		g_AICons[index] = g_IoSCons[make_pair(_curIP, index)];
	}
	else {
		int val = 0;
		int count = g_IoSLists.size();
		int idx = g_nMasterInx;
		for (int i = 0; i < count; i++) {
			val += g_IoSCons[make_pair(g_IoSLists[idx], index)];
			idx = (idx + 1) % count;
			if (idx == g_nPreMasterInx)
				break;
		}

		g_AICons[index] = val;
	}
	std::cout << "sendU[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << std::endl;

	delete[] buf;
}

void IoS::RestartClientConnection(int index)		//Master가 죽고, Slave가 새로운 master가 됐을때, 새로운 master가 R2들에게 연결
{
	if (index < 0 || index >= g_AIIPs.size() - 1)
		return;

	if (_bEnableAIs[index] == true) 
		return;

	if (_masAISocks[index] == INVALID_SOCKET) {
		SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET)
			return;
		_masAISocks[index] = sock;
	}

	if (_bMasAIConnecteds[index] == false) {
		if (Connect(_masAISocks[index], (char*)g_AIIPs[index].c_str(), 9005, 1)) {
			_bMasAIConnecteds[index] = true;
		}
	}

	TTCPPacketHeader header;
	::memcpy(&header.code, TCPSessionParser::PACKET_MAGIC_CODE, 4);
	header.contentLength = 1;

	if (::send(_masAISocks[index], (const char*)&header, sizeof(header), 0) != sizeof(header)) {
		return;
	}

	uint8_t data = 1;
	if (::send(_masAISocks[index], (const char*)&data, 1, 0) != 1) {
		return;
	}

	_bEnableAIs[index] = true;
}

void IoS::StartSystem(std::string evtIP, std::string curIP)
{
	if (evtIP != _evtIP || curIP != _curIP)
		return;

	if (_pDBManager) {
		delete _pDBManager;
		_pDBManager = nullptr;
	}

	_pDBManager = new DataBaseManager(evtIP, curIP);
	if (!_pDBManager)
		return;

	if (!_pDBManager->CreateDBprocess()) {
		delete _pDBManager;
		_pDBManager = nullptr;
		return;
	}

	UpdateProcess(0, 1);

	std::vector<ChInfo_t> Cameras;

	std::vector<ChannelInfoDataSet*> chInfo = _pDBManager->_pDatabaseProcess->_ChannelInfoDataBuff;
	std::vector<RoiInfoDataSet*> roiInfo = _pDBManager->_pDatabaseProcess->_RoiInfoDataBuff;
	std::vector<RoiPointDataSet*> roiPt = _pDBManager->_pDatabaseProcess->_RoiPointInfoDataBuff;
	std::vector<AlgCamOptionInfoDataSet*> algCamOptInfo = _pDBManager->_pDatabaseProcess->_AlgCamOptionInfoDataBuff;

	GroupChannelManager grpChMgr(MAX_DNN_COUNT);
	if (chInfo.size() > 0) {
		int count = (chInfo.size() > MAX_REAL_CAMERA_COUNT) ? MAX_REAL_CAMERA_COUNT : (int)chInfo.size();
		Cameras.resize(count);

		for (int i = 0; i < count; i++) {
			Cameras[i].strChName = chInfo[i]->getCamera_name();
			Cameras[i].ch_id = chInfo[i]->getCamera_id();
			Cameras[i].ch_seq = chInfo[i]->getCamera_seq();
			Cameras[i].is_ptz = chInfo[i]->getSupport_ptz();
			Cameras[i].rtsp_address[0] = chInfo[i]->getRtsp_address(0);
			Cameras[i].rtsp_address[1] = chInfo[i]->getRtsp_address(1);
			Cameras[i].login_id = chInfo[i]->getLogin_id();
			Cameras[i].login_pass = chInfo[i]->getLogin_pw();
			Cameras[i].status = chInfo[i]->getErr_status();

			Cameras[i].alg_mode = InitCamAlgMode(Cameras[i].ch_seq);

			for (int j = 0; j < roiInfo.size(); j++) {
				if (Cameras[i].ch_seq == roiInfo[j]->getCamera_seq()) {
					int rIdx = roiInfo[j]->getRoi_idx();
					if (rIdx < 0 || rIdx >= MAX_ROI_COUNT)
						continue;
					Cameras[i].roi_Info[rIdx].algMask = roiInfo[j]->getAlg_mask();
					int seq = roiInfo[j]->getRoi_seq();

					for (int k = 0; k < roiPt.size(); k++) {
						if (roiPt[k]->getRoi_seq() == seq) {
							Cameras[i].roi_Info[rIdx].roiPts.push_back(CPoint(roiPt[k]->getPt_x(), roiPt[k]->getPt_y()));
						}
					}
				}
			}

			for (int j = 0; j < algCamOptInfo.size(); j++) {
				if (Cameras[i].ch_seq == algCamOptInfo[j]->getCamera_seq()) {
					int dayNight = algCamOptInfo[j]->getDaynight();
					if (dayNight >= 0 && dayNight <= 1) {
						Cameras[i].cam_opt[dayNight].objMin = algCamOptInfo[j]->getObject_min();
						Cameras[i].cam_opt[dayNight].objMax = algCamOptInfo[j]->getObject_max();
						Cameras[i].cam_opt[dayNight].thresMin = algCamOptInfo[j]->getThreshold_min();
						Cameras[i].cam_opt[dayNight].thresMax = algCamOptInfo[j]->getThreshold_max();
						Cameras[i].cam_opt[dayNight].chgRateMax = algCamOptInfo[j]->getChg_Rate_max();
					}
				}
			}

			vector<int> camDurTimes;
			camDurTimes.resize(MAX_ALG_COUNT);

			InitCamEventDurTime(Cameras[i].ch_seq, camDurTimes);
			Cameras[i].durTimes = camDurTimes;

			GroupChannelInfo* pGrpCh = grpChMgr.getGroup(i % MAX_DNN_COUNT);
			if (pGrpCh) {
				pGrpCh->addChannel(Cameras[i].ch_seq);
				Cameras[i].group_index = i % MAX_DNN_COUNT;
			}
			else {
				Cameras[i].group_index = -1;
			}
		}

		for (int i = 0; i < MAX_DNN_COUNT; i++) {
			GroupChannelInfo* pGrpCh = grpChMgr.getGroup(i);
			pGrpCh->UpdateFrame();
		}

		_ConnectInfo.Cameras = Cameras;
	}

	_grpChMgr = grpChMgr;

	vector<int> durTimes(MAX_ALG_COUNT);
	InitEventDurTime(durTimes);

	_ConnectInfo.evtDurTimes = durTimes;

	if (_pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff.size() > 0) {
		_ConnectInfo.algOpt.objMin = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getObject_min();
		_ConnectInfo.algOpt.objMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getObject_max();
		_ConnectInfo.algOpt.thresMin = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getThreshold_min();
		_ConnectInfo.algOpt.thresMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getThreshold_max();
		_ConnectInfo.algOpt.chgRateMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getChg_Rate_max();
	}

	StartAnalysisAll();

	UpdateProcess(1, 1);
}

void IoS::StopAnalyses()
{
	StopAnalysisAll();
//	StopLBClient();

	UpdateProcess(1, 2);
	ResetProcess();

	_ConnectInfo.Cameras.clear();

	if (_pDBManager) {
		delete _pDBManager;
		_pDBManager = nullptr;
	}
}

void IoS::StartAlgAll()
{
	UpdateProcess(3, 1);
}

void IoS::StopAlgAll()
{
	UpdateProcess(3, 2);
}

void IoS::ApplyAlgAll()		//kimdh0817 - 설정적용
{
	CmdHeader Header;
	::memset(&Header, 0x00, sizeof(CmdHeader));
	Header.cmd = 0x01;

	for (int i = 0; i < _cliAIs.size(); i++) {
		if (_cliAIs[i].connect(g_AIIPs[i].c_str(), 9004, 1)) {
			_cliAIs[i].send((uint8_t*)&Header, sizeof(CmdHeader));
			_cliAIs[i].disconnect();
		}
	}

	StopAnalysisAll();
	//	StopLBClient();

	Sleep(100);

	_ConnectInfo.Cameras.clear();

	if (_pDBManager) {
		delete _pDBManager;
		_pDBManager = nullptr;
	}

	//----------------------------------------------------------------------------------------------------------------------------------

	_pDBManager = new DataBaseManager(_evtIP, _curIP);
	if (!_pDBManager)
		return;

	if (!_pDBManager->CreateDBprocess()) {
		delete _pDBManager;
		_pDBManager = nullptr;
		return;
	}

	std::vector<ChInfo_t> Cameras;

	std::vector<ChannelInfoDataSet*> chInfo = _pDBManager->_pDatabaseProcess->_ChannelInfoDataBuff;
	std::vector<RoiInfoDataSet*> roiInfo = _pDBManager->_pDatabaseProcess->_RoiInfoDataBuff;
	std::vector<RoiPointDataSet*> roiPt = _pDBManager->_pDatabaseProcess->_RoiPointInfoDataBuff;
	std::vector<AlgCamOptionInfoDataSet*> algCamOptInfo = _pDBManager->_pDatabaseProcess->_AlgCamOptionInfoDataBuff;

	GroupChannelManager grpChMgr(MAX_DNN_COUNT);
	if (chInfo.size() > 0) {
		int count = (chInfo.size() > MAX_REAL_CAMERA_COUNT) ? MAX_REAL_CAMERA_COUNT : (int)chInfo.size();
		Cameras.resize(count);

		for (int i = 0; i < count; i++) {
			Cameras[i].strChName = chInfo[i]->getCamera_name();
			Cameras[i].ch_id = chInfo[i]->getCamera_id();
			Cameras[i].ch_seq = chInfo[i]->getCamera_seq();
			Cameras[i].is_ptz = chInfo[i]->getSupport_ptz();
			Cameras[i].rtsp_address[0] = chInfo[i]->getRtsp_address(0);
			Cameras[i].rtsp_address[1] = chInfo[i]->getRtsp_address(1);
			Cameras[i].login_id = chInfo[i]->getLogin_id();
			Cameras[i].login_pass = chInfo[i]->getLogin_pw();
			Cameras[i].status = chInfo[i]->getErr_status();

			Cameras[i].alg_mode = InitCamAlgMode(Cameras[i].ch_seq);

			for (int j = 0; j < roiInfo.size(); j++) {
				if (Cameras[i].ch_seq == roiInfo[j]->getCamera_seq()) {
					int rIdx = roiInfo[j]->getRoi_idx();
					if (rIdx < 0 || rIdx >= MAX_ROI_COUNT)
						continue;
					Cameras[i].roi_Info[rIdx].algMask = roiInfo[j]->getAlg_mask();
					int seq = roiInfo[j]->getRoi_seq();

					for (int k = 0; k < roiPt.size(); k++) {
						if (roiPt[k]->getRoi_seq() == seq) {
							Cameras[i].roi_Info[rIdx].roiPts.push_back(CPoint(roiPt[k]->getPt_x(), roiPt[k]->getPt_y()));
						}
					}
				}
			}

			for (int j = 0; j < algCamOptInfo.size(); j++) {
				if (Cameras[i].ch_seq == algCamOptInfo[j]->getCamera_seq()) {
					int dayNight = algCamOptInfo[j]->getDaynight();
					if (dayNight >= 0 && dayNight <= 1) {
						Cameras[i].cam_opt[dayNight].objMin = algCamOptInfo[j]->getObject_min();
						Cameras[i].cam_opt[dayNight].objMax = algCamOptInfo[j]->getObject_max();
						Cameras[i].cam_opt[dayNight].thresMin = algCamOptInfo[j]->getThreshold_min();
						Cameras[i].cam_opt[dayNight].thresMax = algCamOptInfo[j]->getThreshold_max();
						Cameras[i].cam_opt[dayNight].chgRateMax = algCamOptInfo[j]->getChg_Rate_max();
					}
				}
			}

			vector<int> camDurTimes;
			camDurTimes.resize(MAX_ALG_COUNT);

			InitCamEventDurTime(Cameras[i].ch_seq, camDurTimes);
			Cameras[i].durTimes = camDurTimes;

			GroupChannelInfo* pGrpCh = grpChMgr.getGroup(i % MAX_DNN_COUNT);
			if (pGrpCh) {
				pGrpCh->addChannel(Cameras[i].ch_seq);
				Cameras[i].group_index = i % MAX_DNN_COUNT;
			}
			else {
				Cameras[i].group_index = -1;
			}
		}

		for (int i = 0; i < MAX_DNN_COUNT; i++) {
			GroupChannelInfo* pGrpCh = grpChMgr.getGroup(i);
			pGrpCh->UpdateFrame();
		}

		_ConnectInfo.Cameras = Cameras;
	}

	_grpChMgr = grpChMgr;

	vector<int> durTimes(MAX_ALG_COUNT);
	InitEventDurTime(durTimes);

	_ConnectInfo.evtDurTimes = durTimes;

	if (_pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff.size() > 0) {
		_ConnectInfo.algOpt.objMin = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getObject_min();
		_ConnectInfo.algOpt.objMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getObject_max();
		_ConnectInfo.algOpt.thresMin = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getThreshold_min();
		_ConnectInfo.algOpt.thresMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getThreshold_max();
		_ConnectInfo.algOpt.chgRateMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getChg_Rate_max();
	}

	StartAnalysisAll();
	Sleep(100);
}

void IoS::ApplyAlgCamOpt(int ch_seq)
{
	_pDBManager->_pDatabaseProcess->_AlgCamOptionInfoDataBuff.clear();
	_pDBManager->ReSetDataforAlgCamOptionInfoTB();

	std::vector<AlgCamOptionInfoDataSet*> algCamOptInfo = _pDBManager->_pDatabaseProcess->_AlgCamOptionInfoDataBuff;
	int index = -1;
	for (int i = 0; i < _ConnectInfo.Cameras.size(); i++) {
		if (ch_seq == _ConnectInfo.Cameras[i].ch_seq) {
			index = i;
			break;
		}
	}

	if (index < 0)
		return;

	InitCamAlgModeEtc(ch_seq, _ConnectInfo.Cameras[index]);

	if (GetRoiInfo(_ConnectInfo.Cameras[index].ch_seq, _ConnectInfo.Cameras[index].roi_Info) == false)
		return;

	if (_ConnectInfo.Cameras[index].alg_mode == 1) {
		if (GetCamOptInfo(_ConnectInfo.Cameras[index].ch_seq, _ConnectInfo.Cameras[index].cam_opt) == false)
			return;

		vector<int> camDurTimes;
		camDurTimes.resize(MAX_ALG_COUNT);

		InitCamEventDurTime(ch_seq, camDurTimes);
		_ConnectInfo.Cameras[index].durTimes = camDurTimes;
	}
	else {
		for (int i = 0; i < 2; i++) {
			_ConnectInfo.Cameras[index].cam_opt[0].objMin = _ConnectInfo.algOpt.objMin;
			_ConnectInfo.Cameras[index].cam_opt[1].objMin = _ConnectInfo.algOpt.objMin;
			_ConnectInfo.Cameras[index].cam_opt[0].objMax = _ConnectInfo.algOpt.objMax;
			_ConnectInfo.Cameras[index].cam_opt[1].objMax = _ConnectInfo.algOpt.objMax;
			_ConnectInfo.Cameras[index].cam_opt[0].thresMin = _ConnectInfo.algOpt.thresMin;
			_ConnectInfo.Cameras[index].cam_opt[1].thresMin = _ConnectInfo.algOpt.thresMin;
			_ConnectInfo.Cameras[index].cam_opt[0].thresMax = _ConnectInfo.algOpt.thresMax;
			_ConnectInfo.Cameras[index].cam_opt[1].thresMax = _ConnectInfo.algOpt.thresMax;
			_ConnectInfo.Cameras[index].cam_opt[0].chgRateMax = _ConnectInfo.algOpt.chgRateMax;
			_ConnectInfo.Cameras[index].cam_opt[1].chgRateMax = _ConnectInfo.algOpt.chgRateMax;
		}
		_ConnectInfo.Cameras[index].durTimes = _ConnectInfo.evtDurTimes;
	}

	int gdx = _ConnectInfo.Cameras[index].group_index;
	if (gdx < 0)
		return;

	std::unique_lock<std::mutex> lock(_mtx);
	for (int i = 0; i < _groupStreams[gdx].size(); i++) {
		DeviceStream* pStream = _groupStreams[gdx][i];
		if (pStream && pStream->channelSeq() == ch_seq) {
			pStream->SetCamInfo(_ConnectInfo.Cameras[index]);
			break;
		}
	}
	lock.unlock();

	CmdHeader Header;
	::memset(&Header, 0x00, sizeof(CmdHeader));
	Header.cmd = 0x01;
	Header.type = 0x01;
	Header.data_size = sizeof(int);

	for (int i = 0; i < _cliAIs.size(); i++) {
		if (_cliAIs[i].connect(g_AIIPs[i].c_str(), 9004, 1)) {
			_cliAIs[i].send((uint8_t*)&Header, sizeof(CmdHeader));
			_cliAIs[i].send((uint8_t*)&ch_seq, sizeof(int));
			_cliAIs[i].disconnect();
		}
	}
}

void IoS::StartAlgCamera(std::string camList)
{
	CString strList(camList.c_str());
	CString strDiv;
	int index = 0;

	while (1) {
		if (::AfxExtractSubString(strDiv, strList, index, ',') == FALSE)
			break;
		index++;

		int ch_seq = _ttoi(strDiv);
		for (int i = 0; i < _ConnectInfo.Cameras.size(); i++) {
			if (_ConnectInfo.Cameras[i].ch_seq == ch_seq) {
				break;
			}
		}
	}
}

void IoS::StopAlgCamera(std::string camList)
{
	CString strList(camList.c_str());
	CString strDiv;
	int index = 0;

	while (1) {
		if (::AfxExtractSubString(strDiv, strList, index, ',') == FALSE)
			break;
		index++;

		int ch_seq = _ttoi(strDiv);
		for (int i = 0; i < _ConnectInfo.Cameras.size(); i++) {
			if (_ConnectInfo.Cameras[i].ch_seq == ch_seq) {
				break;
			}
		}
	}
}

void IoS::AddCamera(std::string camList, int mode)
{
	CString sCamList(camList.c_str());

	CString sTok;
	int index = 0;

	do {
		if (!AfxExtractSubString(sTok, sCamList, index++, ','))
			break;

		int ch_seq = _ttoi(sTok);

		if (mode == 1) {
			uint8_t data[64] = { 0, };
			CmdHeader* pHeader = (CmdHeader*)data;
			pHeader->cmd = 0x02;
			pHeader->type = 0x00;
			pHeader->data_size = sizeof(int);
			::memcpy(&data[sizeof(CmdHeader)], &ch_seq, sizeof(int));

			for (int i = 0; i < _cliAIs.size(); i++) {
				if (_cliAIs[i].connect(g_AIIPs[i].c_str(), 9004, 1)) {
					_cliAIs[i].send(data, sizeof(CmdHeader) + sizeof(int));
					_cliAIs[i].disconnect();
				}
			}
		}

		if (_ConnectInfo.Cameras.size() >= MAX_REAL_CAMERA_COUNT)
			continue;

		bool bExist = false;
		for (int i = 0; i < _ConnectInfo.Cameras.size(); i++) {
			if (ch_seq == _ConnectInfo.Cameras[i].ch_seq) {
				bExist = true;
				break;
			}
		}

		if (bExist)
			continue;

		if (GetCameraInfo(ch_seq) == false)
			continue;

		_Camera.alg_mode = InitCamAlgMode(ch_seq);

		if (GetRoiInfo(_Camera.ch_seq) == false)
			continue;

		if (_Camera.alg_mode == 1) {
			if (GetCamOptInfo(_Camera.ch_seq) == false)
				continue;

			vector<int> camDurTimes;
			camDurTimes.resize(MAX_ALG_COUNT);

			InitCamEventDurTime(ch_seq, camDurTimes);
			_Camera.durTimes = camDurTimes;
		}
		else {
			for (int i = 0; i < 2; i++) {
				_Camera.cam_opt[i].objMin = _ConnectInfo.algOpt.objMin;
				_Camera.cam_opt[i].objMax = _ConnectInfo.algOpt.objMax;
				_Camera.cam_opt[i].thresMin = _ConnectInfo.algOpt.thresMin;
				_Camera.cam_opt[i].thresMax = _ConnectInfo.algOpt.thresMax;
				_Camera.cam_opt[i].chgRateMax = _ConnectInfo.algOpt.chgRateMax;
			}

			_Camera.durTimes = _ConnectInfo.evtDurTimes;
		}

		GroupChannelInfo* pGrpCh = _grpChMgr.getGroup(0);

		int gdx = 0;
		int channels = pGrpCh->getCount();

		for (int i = 1; i < MAX_DNN_COUNT; i++) {
			pGrpCh = _grpChMgr.getGroup(i);
			if (channels > pGrpCh->getCount()) {
				channels = pGrpCh->getCount();
				gdx = i;
			}
		}
		_Camera.group_index = gdx;
		_Camera.status = 0;
		_ConnectInfo.Cameras.push_back(_Camera);

		pGrpCh = _grpChMgr.getGroup(gdx);
		pGrpCh->addChannel(ch_seq);
		pGrpCh->UpdateFrame();

		std::string host, uri;
		uint16_t rtsp_port;

		for (int i = 0; i < _groupStreams[gdx].size(); i++) {
			DeviceStream* pStream = _groupStreams[gdx][i];
			if (pStream && pStream->IsIdleState()) {
				pStream->SetCamInfo(_Camera);
				std::string addr = _Camera.rtsp_address[1].size() > 0 ? _Camera.rtsp_address[1] : _Camera.rtsp_address[0];
				if (GetRtspAddr(addr, host, rtsp_port, uri))
					pStream->Connect(host, rtsp_port, uri, _Camera.login_id, _Camera.login_pass);

				break;
			}
		}
	} while (1);
}

void IoS::RemoveCamera(std::string camList, int mode)
{
	CString sCamList(camList.c_str());

	CString sTok;
	int index = 0;

	do {
		if (!AfxExtractSubString(sTok, sCamList, index++, ','))
			break;

		int ch_seq = _ttoi(sTok);

		if (mode == 1) {
			uint8_t data[64] = { 0, };
			CmdHeader* pHeader = (CmdHeader*)data;
			pHeader->cmd = 0x02;
			pHeader->type = 0x01;
			pHeader->data_size = sizeof(int);
			::memcpy(&data[sizeof(CmdHeader)], &ch_seq, sizeof(int));
			for (int i = 0; i < _cliAIs.size(); i++) {
				if (_cliAIs[i].connect(g_AIIPs[i].c_str(), 9004, 1)) {
					_cliAIs[i].send(data, sizeof(CmdHeader) + sizeof(int));
					_cliAIs[i].disconnect();
				}
			}
		}

		bool bExist = false;
		for (int i = 0; i < _ConnectInfo.Cameras.size(); i++) {
			if (ch_seq == _ConnectInfo.Cameras[i].ch_seq) {
				bExist = true;
				break;
			}
		}

		if (!bExist)
			continue;

		int gdx = -1;
		GroupChannelInfo* pGrpCh = nullptr;

		for (int i = 0; i < MAX_DNN_COUNT; i++) {
			pGrpCh = _grpChMgr.getGroup(i);
			if (pGrpCh->FindChannel(ch_seq)) {
				gdx = i;
				break;
			}
		}

		if (gdx < 0)
			continue;

		std::unique_lock<std::mutex> lock(_mtx);
		for (int i = 0; i < _groupStreams[gdx].size(); i++) {
			DeviceStream* pStream = _groupStreams[gdx][i];
			if (pStream && pStream->IsIdleState() == false) {
				if (pStream->channelSeq() == ch_seq) {
					pStream->DisconnectAll();
//					pStream->StopThreads();
					break;
				}
			}
		}
		lock.unlock();

		pGrpCh = _grpChMgr.getGroup(gdx);
		pGrpCh->delChannel(ch_seq);
		pGrpCh->UpdateFrame();

		for (int i = 0; i < _ConnectInfo.Cameras.size(); i++) {
			if (_ConnectInfo.Cameras[i].ch_seq == ch_seq) {
				_ConnectInfo.Cameras.erase(_ConnectInfo.Cameras.begin() + i);
				break;
			}
		}
	} while (1);
}

bool IoS::GetCameraInfo(int ch_seq)
{
	if (_evtIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = std::string("select camera_name, camera_id, id, pw, rtsp_address, rtsp_address2, support_ptz, cam.err_stat from \"CAMERAS\" CAM, \"CURATOR_CONNECTION\" CC where CAM.use_yn = '1' and CAM.curation_server_id = CC.curator_seq and CC.curator_ip = '") + _curIP + std::string("' and CAM.cameras_seq = '") + std::to_string(ch_seq) + std::string("'");

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;

	ChInfo_t Camera;
	int count = _ttoi(sCount);
	if (count != 1)
		return false;

	AfxExtractSubString(sSingleRecord, sWholeRecord, 0, '|');

//	if (sSingleRecord.GetAt(0) == '$')
//		return false;

	int index = 0;

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.strChName = sTok;

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.ch_id = std::string(CT2CA(sTok));

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.login_id = std::string(CT2CA(sTok));

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.login_pass = std::string(CT2CA(sTok));

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.rtsp_address[0] = std::string(CT2CA(sTok));

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.rtsp_address[1] = std::string(CT2CA(sTok));

	if (Camera.rtsp_address[0].empty() && Camera.rtsp_address[1].empty())
		return false;

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.is_ptz = (std::string(CT2CA(sTok)) == "true") ? true : false;

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.status = _ttoi(sTok);

	Camera.ch_seq = ch_seq;

	_Camera = Camera;

	return true;
}

void IoS::EnableCameraState()
{
	for (int gdx = 0; gdx < MAX_DNN_COUNT; gdx++) {
		std::unique_lock<std::mutex> lock(_mtx);
		for (int i = 0; i < _groupStreams[gdx].size(); i++) {
			DeviceStream* pStream = _groupStreams[gdx][i];
			if (pStream)
				pStream->ResetCameraState();
		}
		lock.unlock();
	}
}

uint32_t IoS::ParseAlgMask(CString sTok)
{
	uint32_t mask = 0;

	CString sDiv;

	for (int i = 0; i < MAX_ALG_COUNT; i++) {
		if (::AfxExtractSubString(sDiv, sTok, i, '^')) {
			if (sDiv.CompareNoCase(L"alg01") == 0)
				mask |= (0x1 << 1);
			else if (sDiv.CompareNoCase(L"alg02") == 0)
				mask |= (0x1 << 2);
			else if (sDiv.CompareNoCase(L"alg03") == 0)
				mask |= (0x1 << 3);
			else if (sDiv.CompareNoCase(L"alg04") == 0)
				mask |= (0x1 << 4);
			else if (sDiv.CompareNoCase(L"alg05") == 0)
				mask |= (0x1 << 5);
			else if (sDiv.CompareNoCase(L"alg06") == 0)
				mask |= (0x1 << 6);
			else if (sDiv.CompareNoCase(L"alg07") == 0)
				mask |= (0x1 << 7);
			else if (sDiv.CompareNoCase(L"alg08") == 0)
				mask |= (0x1 << 8);
			else if (sDiv.CompareNoCase(L"alg09") == 0)
				mask |= (0x1 << 9);
			else if (sDiv.CompareNoCase(L"alg10") == 0)
				mask |= (0x1 << 10);
			else if (sDiv.CompareNoCase(L"alg11") == 0)
				mask |= (0x1 << 11);
			else if (sDiv.CompareNoCase(L"alg12") == 0)
				mask |= (0x1 << 12);
			else if (sDiv.CompareNoCase(L"alg13") == 0)
				mask |= (0x1 << 13);
			else if (sDiv.CompareNoCase(L"alg14") == 0)
				mask |= (0x1 << 14);
			else if (sDiv.CompareNoCase(L"alg15") == 0)
				mask |= (0x1 << 15);
			else if (sDiv.CompareNoCase(L"alg16") == 0)
				mask |= (0x1 << 16);
			else if (sDiv.CompareNoCase(L"alg20") == 0)
				mask |= (0x1 << 20);
		}
	}

	return mask;
}

bool IoS::GetRoiInfo(int ch_seq)
{
	if (_evtIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = std::string("select roi_idx, roi_seq, alg_id1, angle from \"ROI_INFO\" where cameras_seq = '") + std::to_string(ch_seq) + std::string("'");

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;

	int count = _ttoi(sCount);


	int roi_idx = -1;
	int roi_seq[2] = { -1, -1 };
	RoiInfo_t roi_Info[2];

	for (int i = 0; i < count; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			return false;

		int index = 0;
		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		roi_idx = _ttoi(sTok);
		if (roi_idx < 0 || roi_idx > 1)
			continue;

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		roi_seq[roi_idx] = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;

		roi_Info[roi_idx].algMask = ParseAlgMask(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		roi_Info[roi_idx].angle = _ttoi(sTok);
	}

	for (int i = 0; i < 2; i++) {
		if (roi_seq[i] < 0)
			continue;

		roi_Info[i].roiPts = GetRoiPtInfo(roi_seq[i]);
	}

	for (int i = 0; i < 2; i++)
		_Camera.roi_Info[i] = roi_Info[i];

	return true;
}

bool IoS::GetRoiInfo(int ch_seq, RoiInfo_t roiInfo[])
{
	if (_evtIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = std::string("select roi_idx, roi_seq, alg_id1, angle from \"ROI_INFO\" where cameras_seq = '") + std::to_string(ch_seq) + std::string("'");

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;

	int count = _ttoi(sCount);


	int roi_idx = -1;
	int roi_seq[MAX_ROI_COUNT];
	RoiInfo_t roi_Info[MAX_ROI_COUNT];

	for (int i = 0; i < MAX_ROI_COUNT; i++)
		roi_seq[i] = -1;

	for (int i = 0; i < count; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			return false;

		int index = 0;
		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		roi_idx = _ttoi(sTok);
		if (roi_idx < 0 || roi_idx >= MAX_ROI_COUNT)
			continue;

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		roi_seq[roi_idx] = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;

		roi_Info[roi_idx].algMask = ParseAlgMask(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		roi_Info[roi_idx].angle = _ttoi(sTok);
	}

	for (int i = 0; i < MAX_ROI_COUNT; i++) {
		if (roi_seq[i] < 0)
			continue;

		roi_Info[i].roiPts = GetRoiPtInfo(roi_seq[i]);
	}

	for (int i = 0; i < MAX_ROI_COUNT; i++)
		roiInfo[i] = roi_Info[i];

	return true;
}

std::vector<CPoint> IoS::GetRoiPtInfo(int roi_seq)
{
	std::vector<CPoint> pts, pts2;
	CPoint pt;

	if (_evtIP.empty())
		return pts;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return pts;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return pts;
		}
	}

	std::string query = std::string("select pt_x, pt_y from \"ROI_POINT\" where roi_seq = ") + std::to_string(roi_seq) + std::string("order by pt_seq");

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return pts;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;

	int count = _ttoi(sCount);

	for (int i = 0; i < count; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			return pts;

		int index = 0;
		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return pts;
		pt.x = _ttoi(sTok);
		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return pts;
		pt.y = _ttoi(sTok);

		pts2.push_back(pt);
	}

	pts = pts2;

	return pts;
}

bool IoS::GetCamOptInfo(int ch_seq)
{
	if (_evtIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = std::string("select daynight, obj_min, obj_max, threshold_min, threshold_max, change_rate from \"CAMERA_SETTING\" where cameras_seq = '") + std::to_string(ch_seq) + std::string("'");

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;

	int count = _ttoi(sCount);

	AlgOpt_t opt[2];
	memset(opt, 0x00, sizeof(opt));

	for (int i = 0; i < count; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			return false;

		int index = 0;
		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		int daynight = _ttoi(sTok);
		if (daynight < 0 || daynight > 1)
			continue;

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].objMin = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].objMax = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].thresMin = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].thresMax = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].chgRateMax = _ttoi(sTok);
	}

	::memcpy(_Camera.cam_opt, opt, sizeof(opt));

	return true;
}

bool IoS::GetCamOptInfo(int ch_seq, AlgOpt_t Opt[])
{
	if (_evtIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = std::string("select daynight, obj_min, obj_max, threshold_min, threshold_max, change_rate from \"CAMERA_SETTING\" where cameras_seq = '") + std::to_string(ch_seq) + std::string("'");

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;

	int count = _ttoi(sCount);

	AlgOpt_t opt[2];
	memset(opt, 0x00, sizeof(opt));

	for (int i = 0; i < count; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			return false;

		int index = 0;
		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		int daynight = _ttoi(sTok);
		if (daynight < 0 || daynight > 1)
			continue;

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].objMin = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].objMax = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].thresMin = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].thresMax = _ttoi(sTok);

		if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
			return false;
		opt[daynight].chgRateMax = _ttoi(sTok);
	}

	::memcpy(Opt, opt, sizeof(opt));

	return true;
}

void IoS::ResetProcess()
{
	CDBPostgre* p_DbPostgre = nullptr;

	p_DbPostgre = new CDBPostgre(_evtIP);

	if (!p_DbPostgre)
		return;

	if (!p_DbPostgre->_pDB) {
		delete p_DbPostgre;
		return;
	}
	else {
		ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			p_DbPostgre->_pDB->CloseConnection();
			delete p_DbPostgre;
			return;
		}
	}

	std::string query = "UPDATE \"CURATION_PROCESS\" SET send_curation=0 op_curation=0 send_alg=0 op_alg=0";
	p_DbPostgre->dbUpdate(query);

	p_DbPostgre->_pDB->CloseConnection();

	delete p_DbPostgre;
}

void IoS::UpdateProcess(int index, int value)
{
	if (index < 0 || index > 5)
		return;

	CDBPostgre* p_DbPostgre = new CDBPostgre(_evtIP);

	if (!p_DbPostgre)
		return;

	if (!p_DbPostgre->_pDB) {
		delete p_DbPostgre;
		return;
	}
	else {
		ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			p_DbPostgre->_pDB->CloseConnection();
			delete p_DbPostgre;
			return;
		}
	}

	CString strSub;

	if (index == 0) {
		strSub.Format(_T("send_curation=%d"), value);
		_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setSend_curation(value);
	}
	else if (index == 1) {
		if (value == 2) {
			strSub.Format(_T("send_curation=2, op_curation=2, send_alg=2, op_alg=2, send_config=0, op_config=0"));
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setSend_curation(2);
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setOp_curation(2);
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setSend_alg(2);
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setOp_alg(2);
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setSend_config(0);
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setOp_config(0);
		}
		else {
			strSub.Format(_T("op_curation=%d"), value);
			if (_pDBManager && _pDBManager->_pDatabaseProcess && _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff.size() > 0)
				_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setOp_curation(value);
		}
	}
	else if (index == 2) {
		strSub.Format(_T("send_alg=%d"), value);
		if (_pDBManager && _pDBManager->_pDatabaseProcess && _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff.size() > 0)
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setSend_alg(value);
	}
	else if (index == 3) {
		strSub.Format(_T("op_alg=%d"), value);
		if (_pDBManager && _pDBManager->_pDatabaseProcess && _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff.size() > 0)
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setOp_alg(value);
	}
	else if (index == 4) {
		if (_pDBManager && _pDBManager->_pDatabaseProcess && _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff.size() > 0) {
			if (value < 0)
				value = (_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getSend_config() == 1) ? 2 : 1;
			strSub.Format(_T("send_config=%d"), value);
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setSend_config(value);
		}
	}
	else if (index == 5) {
		if (_pDBManager && _pDBManager->_pDatabaseProcess && _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff.size() > 0) {
			if (value < 0)
				value = _pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getSend_config();
			strSub.Format(_T("op_config=%d"), value);
			_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->setOp_config(value);
		}
	}

	if (strSub.IsEmpty()) {
		p_DbPostgre->_pDB->CloseConnection();
		delete p_DbPostgre;
		return;
	}

	CString strCmd = _T("UPDATE \"CURATION_PROCESS\" SET ") + strSub + CString(_T(" where svr_ip='")) + CString(_curIP.c_str()) + CString(_T("'"));;

	std::string sCmd = std::string(CT2CA(strCmd));
	p_DbPostgre->dbUpdate(sCmd);

	p_DbPostgre->_pDB->CloseConnection();
	delete p_DbPostgre;
}

void IoS::LoadInitData()
{
	if (_iniFile.Load(_iniFileName.GetBuffer(0)))
	{
		CString evtIPAddress = _iniFile.GetKeyValue(L"Init", L"EventIPAddress").c_str();
		CString ourIPAddress = _iniFile.GetKeyValue(L"Init", L"ourIPAddress").c_str();

#if _DBINI
		CString DBname = _iniFile.GetKeyValue(L"Init", L"DBname").c_str();
		_DBname = std::string(CT2CA(DBname));
#endif
		
		_evtIP = std::string(CT2CA(evtIPAddress));
		_curIP = std::string(CT2CA(ourIPAddress));
	}
	cout << "_evtIP : " << _evtIP << ", _curIP : " << _curIP << endl;
}

void IoS::StartRecovery1()
{
	std::vector<ChInfo_t> Cameras;

	std::vector<ChannelInfoDataSet*> chInfo = _pDBManager->_pDatabaseProcess->_ChannelInfoDataBuff;
	std::vector<RoiInfoDataSet*> roiInfo = _pDBManager->_pDatabaseProcess->_RoiInfoDataBuff;
	std::vector<RoiPointDataSet*> roiPt = _pDBManager->_pDatabaseProcess->_RoiPointInfoDataBuff;
	std::vector<AlgCamOptionInfoDataSet*> algCamOptInfo = _pDBManager->_pDatabaseProcess->_AlgCamOptionInfoDataBuff;

	vector<int> durTimes(MAX_ALG_COUNT);
	InitEventDurTime(durTimes);

	_ConnectInfo.evtDurTimes = durTimes;

	if (_pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff.size() > 0) {
		_ConnectInfo.algOpt.objMin = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getObject_min();
		_ConnectInfo.algOpt.objMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getObject_max();
		_ConnectInfo.algOpt.thresMin = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getThreshold_min();
		_ConnectInfo.algOpt.thresMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getThreshold_max();
		_ConnectInfo.algOpt.chgRateMax = _pDBManager->_pDatabaseProcess->_AlgOptionInfoDataBuff[0]->getChg_Rate_max();
	}

	GroupChannelManager grpChMgr(MAX_DNN_COUNT);
	if (chInfo.size() > 0) {
		int count = (chInfo.size() > MAX_REAL_CAMERA_COUNT) ? MAX_REAL_CAMERA_COUNT : (int)chInfo.size();
		Cameras.resize(count);

		for (int i = 0; i < count; i++) {
			Cameras[i].strChName = chInfo[i]->getCamera_name();
			Cameras[i].ch_id = chInfo[i]->getCamera_id();
			Cameras[i].ch_seq = chInfo[i]->getCamera_seq();
			Cameras[i].is_ptz = chInfo[i]->getSupport_ptz();
			Cameras[i].rtsp_address[0] = chInfo[i]->getRtsp_address(0);
			Cameras[i].rtsp_address[1] = chInfo[i]->getRtsp_address(1);
			Cameras[i].login_id = chInfo[i]->getLogin_id();
			Cameras[i].login_pass = chInfo[i]->getLogin_pw();

			Cameras[i].alg_mode = InitCamAlgMode(Cameras[i].ch_seq);

			for (int j = 0; j < roiInfo.size(); j++) {
				if (Cameras[i].ch_seq == roiInfo[j]->getCamera_seq()) {
					int rIdx = roiInfo[j]->getRoi_idx();
					if (rIdx < 0 || rIdx >= 2)
						continue;
					Cameras[i].roi_Info[rIdx].algMask = roiInfo[j]->getAlg_mask();
					Cameras[i].roi_Info[rIdx].angle = roiInfo[j]->getAngle();
					int seq = roiInfo[j]->getRoi_seq();

					for (int k = 0; k < roiPt.size(); k++) {
						if (roiPt[k]->getRoi_seq() == seq) {
							Cameras[i].roi_Info[rIdx].roiPts.push_back(CPoint(roiPt[k]->getPt_x(), roiPt[k]->getPt_y()));
							//cout << "Roi_seq : " << roiPt[k]->getRoi_seq() << ", Pt_seq : " << roiPt[k]->getPt_seq() << ", Pt_x : " << roiPt[k]->getPt_x() << ", Pt_y : " << roiPt[k]->getPt_y() << endl;
						}
					}
				}
			}

			if (Cameras[i].alg_mode == 1) {
				for (int j = 0; j < algCamOptInfo.size(); j++) {
					if (Cameras[i].ch_seq == algCamOptInfo[j]->getCamera_seq()) {
						int dayNight = algCamOptInfo[j]->getDaynight();
						if (dayNight >= 0 && dayNight <= 1) {
							Cameras[i].cam_opt[dayNight].objMin = algCamOptInfo[j]->getObject_min();
							Cameras[i].cam_opt[dayNight].objMax = algCamOptInfo[j]->getObject_max();
							Cameras[i].cam_opt[dayNight].thresMin = algCamOptInfo[j]->getThreshold_min();
							Cameras[i].cam_opt[dayNight].thresMax = algCamOptInfo[j]->getThreshold_max();
							Cameras[i].cam_opt[dayNight].chgRateMax = algCamOptInfo[j]->getChg_Rate_max();
						}
					}
				}

				vector<int> camDurTimes;
				camDurTimes.resize(MAX_ALG_COUNT);

				InitCamEventDurTime(Cameras[i].ch_seq, camDurTimes);
				Cameras[i].durTimes = camDurTimes;
			}
			else {
				Cameras[i].cam_opt[0].objMin = _ConnectInfo.algOpt.objMin;
				Cameras[i].cam_opt[1].objMin = _ConnectInfo.algOpt.objMin;
				Cameras[i].cam_opt[0].objMax = _ConnectInfo.algOpt.objMax;
				Cameras[i].cam_opt[1].objMax = _ConnectInfo.algOpt.objMax;
				Cameras[i].cam_opt[0].thresMin = _ConnectInfo.algOpt.thresMin;
				Cameras[i].cam_opt[1].thresMin = _ConnectInfo.algOpt.thresMin;
				Cameras[i].cam_opt[0].thresMax = _ConnectInfo.algOpt.thresMax;
				Cameras[i].cam_opt[1].thresMax = _ConnectInfo.algOpt.thresMax;
				Cameras[i].cam_opt[0].chgRateMax = _ConnectInfo.algOpt.chgRateMax;
				Cameras[i].cam_opt[1].chgRateMax = _ConnectInfo.algOpt.chgRateMax;

				Cameras[i].durTimes = _ConnectInfo.evtDurTimes;
			}

			GroupChannelInfo* pGrpCh = grpChMgr.getGroup(i % MAX_DNN_COUNT);
			if (pGrpCh) {
				pGrpCh->addChannel(Cameras[i].ch_seq);
				Cameras[i].group_index = i % MAX_DNN_COUNT;
			}
			else {
				Cameras[i].group_index = -1;
			}
		}

		for (int i = 0; i < MAX_DNN_COUNT; i++) {
			GroupChannelInfo* pGrpCh = grpChMgr.getGroup(i);
			pGrpCh->UpdateFrame();
		}

		_ConnectInfo.Cameras = Cameras;
	}

	_grpChMgr = grpChMgr;

	StartAnalysisAll();

	if (_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getOp_curation() != 1)
		UpdateProcess(1, 1);

	g_bStartSystem = true;
}

void IoS::StartRecovery2()
{
	if (_pDBManager->_pDatabaseProcess->_ProcessInfoDataBuff[0]->getOp_alg() != 1)
		UpdateProcess(3, 1);

	g_bAlgorithm = true;
}

bool IoS::InitAI()
{
	if (_evtIP.empty())
		return false;

	bool AIout = true;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = "select ai_ip, ai_name, stat from \"AI_CONNECTION\" order by ai_ip";
	//std::string query = "select a.ai_ip, a.ai_name, a.stat from \"AI_CONNECTION\" a RIGHT join \"CURATOR_CONNECTION\" b on a.ai_ip != b.curator_ip order by a.ai_ip";

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		AIout = false;
		//return false;

	}

	if (AIout) {
		dbPostgre._pDB->CloseConnection();

		CString sTok;
		CString sCount;
		CString sWholeRecord;
		CString sSingleRecord;
		//cout << "strResult : " << CT2CA(strResult) << endl;
		AfxExtractSubString(sTok, strResult, 0, ',');
		sCount = sTok;
		//cout << "sCount : " << CT2CA(sCount) << endl;
	//	AfxExtractSubString(sTok, strResult, 1, '#');
		MakeExtractSubString(sTok, strResult, 1, _T("▶"));
		sWholeRecord = sTok;
		//cout << "sWholeRecord : " << CT2CA(sWholeRecord) << endl;
		int nMaxCount = _ttoi(sCount);
		for (int i = 0; i < nMaxCount; i++) {		//외부R2 IP정보
			AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

			if (sSingleRecord.GetAt(0) == '$')
				break;
			 
			int index = 0;
			AfxExtractSubString(sTok, sSingleRecord, index++, '$');
			if (_curIP == std::string(CT2CA(sTok))) {
				cout << "Inner R2 : " << std::string(CT2CA(sTok)) << endl;
				continue;
			}
			g_AIIPs.push_back(std::string(CT2CA(sTok)));

			AfxExtractSubString(sTok, sSingleRecord, index++, '$');
			g_AINames.push_back(sTok);
#if 0
			AfxExtractSubString(sTok, sSingleRecord, index++, '$');
			std::string stat = std::string(CT2CA(sTok));
			if (stat == "0")
				g_AICons.push_back(0);
			else
				g_AICons.push_back(255);
#endif
		}
	}

	g_AIIPs.push_back("127.0.0.1");		//내부R2 IP정보
#if 0
	g_AICons.push_back(0);
#endif

	_cliAIs.resize(g_AIIPs.size());

	for (int i = 0; i < g_AIIPs.size(); i++) {
		SOCKET tmpSock = INVALID_SOCKET;
		_masAISocks.push_back(tmpSock);
		_bMasAIConnecteds.push_back(false);
		_bEnableAIs.push_back(false);
	}

	_lbths.resize(g_AIIPs.size());		//LodeBalance Thread creat
	_bLoopLBs.resize(g_AIIPs.size());	//루프문

	for (int i = 0; i < _bLoopLBs.size(); i++)
		_bLoopLBs[i] = false;

#if 1
	g_AICons.resize(g_AIIPs.size());
	g_AIRecvCons.resize(g_AIIPs.size());
	for (int i = 0; i < g_AIIPs.size(); i++)
		g_AIRecvCons[i] = g_AICons[i] = 255;
#endif

	return true;
}

bool IoS::InitMasterSlave()
{
	if (_evtIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = "select curator_ip, master_gb, curator_name FROM \"CURATOR_CONNECTION\" order by curator_seq";

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;
	cout << "IOS : " << std::string(CT2CA(strResult)) << endl;
//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;
	int nMaxCount = _ttoi(sCount);
	for (int i = 0; i < nMaxCount; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			break;

		int index = 0;
		AfxExtractSubString(sTok, sSingleRecord, index++, '$');
		std::string iosAddr = std::string(CT2CA(sTok));
		if (iosAddr == _curIP)
			g_nIoSInx = i;
		g_IoSLists.push_back(iosAddr);

		AfxExtractSubString(sTok, sSingleRecord, index++, '$');
		std::string master = std::string(CT2CA(sTok));
		if (master == "1")
			g_nPreMasterInx = g_nMasterInx = i;

		AfxExtractSubString(sTok, sSingleRecord, index++, '$');
		g_IoSNames.push_back(sTok);

		for (int ind = 0; ind < g_AICons.size() - 1; ind++)
			g_IoSCons[make_pair(iosAddr, ind)] = 0;
	}
	if (g_nMasterInx < 0) {
		if (ChangeMaster(0) == true) {
			g_nPreMasterInx = g_nMasterInx = 0;
		}
	}
	if (g_nIoSInx == g_nMasterInx) {
		std::cout << "Master1" << std::endl;
		g_bMaster = true;
	}
	else {
		std::cout << " Slave " << std::endl;
		Sleep(5000);		//kimdh0812 - 동시에 켜질때 M-S 레이싱 방지
	}

	return true;
}

int IoS::InitCamAlgMode(int ch_seq)
{
	if (ch_seq < 0)
		return -1;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return -1;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return -1;
		}
	}

	std::string query = "select camera_mode from \"CAMERAS\" where cameras_seq='" + std::to_string(ch_seq) + "'";

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return -1;
	}

	dbPostgre._pDB->CloseConnection();

	CString strDiv1, strDiv2;
//	::AfxExtractSubString(strDiv1, strResult, 1, '#');
	::MakeExtractSubString(strDiv1, strResult, 1, _T("▶"));
	::AfxExtractSubString(strDiv2, strDiv1, 0, '$');

	int mode = _ttoi(strDiv2);

	return mode;
}

bool IoS::InitCamAlgModeEtc(int ch_seq, ChInfo_t& ChInfo)
{
	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = std::string("select camera_name, camera_id, camera_mode from \"CAMERAS\" where cameras_seq='") + std::to_string(ch_seq) + std::string("'");

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	AfxExtractSubString(sTok, strResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, strResult, 1, '#');
	MakeExtractSubString(sTok, strResult, 1, _T("▶"));
	sWholeRecord = sTok;

	ChInfo_t Camera;
	int count = _ttoi(sCount);
	if (count != 1)
		return false;

	AfxExtractSubString(sSingleRecord, sWholeRecord, 0, '|');

	if (sSingleRecord.GetAt(0) == '$')
		return false;

	int index = 0;
	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.strChName = sTok;

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.ch_id = std::string(CT2CA(sTok));

	if (!AfxExtractSubString(sTok, sSingleRecord, index++, '$'))
		return false;
	Camera.alg_mode = _ttoi(sTok);

	Camera.ch_seq = ch_seq;

	ChInfo = Camera;

	return true;
}

int IoS::InitEventDurTime(std::vector<int>& durTimes)
{
	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return -1;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return -1;
		}
	}

	std::string query = "select alg_id, alg_dur from \"alg_dur_info\"";

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT) {
		dbPostgre._pDB->CloseConnection();
		return -1;
	}

	dbPostgre._pDB->CloseConnection();

	int index = 0;
	CString strDiv, strDiv1, strAlg, strTime;

//	::AfxExtractSubString(strDiv, strResult, 1, '#');
	::MakeExtractSubString(strDiv, strResult, 1, _T("▶"));

	while (1) {
		if (::AfxExtractSubString(strDiv1, strDiv, index++, '|') == false)
			break;
		if (strDiv1.GetAt(0) == '$')
			break;
		if (::AfxExtractSubString(strAlg, strDiv1, 0, '$') == false)
			break;
		if (::AfxExtractSubString(strTime, strDiv1, 1, '$') == false)
			break;

		if (strAlg.CompareNoCase(_T("alg00")) == 0)
			durTimes[0] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg01")) == 0)
			durTimes[1] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg02")) == 0)
			durTimes[2] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg03")) == 0)
			durTimes[3] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg04")) == 0)
			durTimes[4] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg05")) == 0)
			durTimes[5] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg06")) == 0)
			durTimes[6] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg07")) == 0)
			durTimes[7] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg08")) == 0)
			durTimes[8] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg09")) == 0)
			durTimes[9] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg10")) == 0)
			durTimes[10] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg11")) == 0)
			durTimes[11] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg12")) == 0)
			durTimes[12] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg13")) == 0)
			durTimes[13] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg14")) == 0)
			durTimes[14] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg15")) == 0)
			durTimes[15] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg16")) == 0)
			durTimes[16] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg20")) == 0)
			durTimes[20] = _ttoi(strTime);
	}

	return 0;
}

int IoS::InitCamEventDurTime(int ch_seq, vector<int>& durTimes)
{
	CDBPostgre dbPostgre(_evtIP);
	if (dbPostgre._pDB == NULL) {
		return -1;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return -1;
		}
	}

	std::string query = "SELECT alg_id, alg_dur FROM \"ALG_DUR_CAM\" where cameras_seq = " + std::to_string(ch_seq);

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= 3) {
		dbPostgre._pDB->CloseConnection();
		return -1;
	}

	dbPostgre._pDB->CloseConnection();

	int index = 0;
	CString strDiv, strDiv1, strAlg, strTime;

//	::AfxExtractSubString(strDiv, strResult, 1, '#');
	::MakeExtractSubString(strDiv, strResult, 1, _T("▶"));

	while (1) {
		if (::AfxExtractSubString(strDiv1, strDiv, index++, '|') == false)
			break;
		if (strDiv1.GetAt(0) == '$')
			break;
		if (::AfxExtractSubString(strAlg, strDiv1, 0, '$') == false)
			break;
		if (::AfxExtractSubString(strTime, strDiv1, 1, '$') == false)
			break;

		if (strAlg.CompareNoCase(_T("alg00")) == 0)
			durTimes[0] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg01")) == 0)
			durTimes[1] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg02")) == 0)
			durTimes[2] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg03")) == 0)
			durTimes[3] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg04")) == 0)
			durTimes[4] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg05")) == 0)
			durTimes[5] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg06")) == 0)
			durTimes[6] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg07")) == 0)
			durTimes[7] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg08")) == 0)
			durTimes[8] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg09")) == 0)
			durTimes[9] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg10")) == 0)
			durTimes[10] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg11")) == 0)
			durTimes[11] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg12")) == 0)
			durTimes[12] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg13")) == 0)
			durTimes[13] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg14")) == 0)
			durTimes[14] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg15")) == 0)
			durTimes[15] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg16")) == 0)
			durTimes[16] = _ttoi(strTime);
		else if (strAlg.CompareNoCase(_T("alg20")) == 0)
			durTimes[20] = _ttoi(strTime);
	}

	return 0;
}

void IoS::StartConnectLogThread()
{
	_bLoopConn = true;
	_th_conn = std::thread{ &IoS::ConnectLogThread, this };
}

void IoS::StopConnectLogThread()
{
	_bLoopConn = false;
	_th_conn.join();
}

void IoS::ConnectLogThread()
{
	int count = 0;
	int len = g_AICons.size();
	std::vector<int> Conns;
	Conns.resize(len + 1);
	Conns[0] = 0;
	for (int i = 0; i < len; i++)
		Conns[i + 1] = g_AICons[i];

	ofstream outFile("Connect.Log");
	outFile << "Inference = (" << std::to_string(Conns[0]) << " / " << std::to_string(_ConnectInfo.Cameras.size()) << ")" << std::endl;
	for (int i = 0; i < len; i++) {
		if (Conns[i + 1] == 255)
			outFile << "IoT-R2[" << std::to_string(i + 1) << "] = Disconnected" << std::endl;
		else
			outFile << "IoT-R2[" << std::to_string(i + 1) << "] = (" << std::to_string(Conns[i + 1]) << " / " << "80)" << std::endl;
	}
	outFile.close();

	while (_bLoopConn)
	{
		bool change = false;
		if (Conns[0] != g_nConnect) {
			Conns[0] = g_nConnect;
			change = true;
		}

		for (int i = 0; i < len; i++) {
			if (Conns[i + 1] != g_AICons[i]) {
				Conns[i + 1] = g_AICons[i];
				change = true;
			}
		}

		if (change) {
			ofstream outFile("Connect.Log");
			outFile << "Inference = (" << std::to_string(Conns[0]) << " / " << std::to_string(_ConnectInfo.Cameras.size()) << ")" << std::endl;
			for (int i = 0; i < len; i++) {
				if (Conns[i + 1] == 255)
					outFile << "IoT-R2[" << std::to_string(i + 1) << "] = Disconnected" << std::endl;
				else
					outFile << "IoT-R2[" << std::to_string(i + 1) << "] = (" << std::to_string(Conns[i + 1]) << " / " << "80)" << std::endl;
			}
			count = (count + 1) % 2;
			if (count == 0)
				outFile << std::endl;
			outFile.close();
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

void IoS::StartPTZLockThread()
{
	_bLoopPTZ = true;
	_th_ptz = std::thread{ &IoS::PTZLockThread, this };
}

void IoS::StopPTZLockThread()
{
	if (_bLoopPTZ == true) {
		_bLoopPTZ = false;
		_th_ptz.join();
	}
}

void IoS::PTZLockThread()
{
	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	while (_bLoopPTZ) {
		std::vector<int> camLists = GetLockLists();
		for (int gdx = 0; gdx < MAX_DNN_COUNT; gdx++) {
			std::unique_lock<std::mutex> lock(_mtx);
			for (int i = 0; i < _groupStreams[gdx].size(); i++) {
				DeviceStream* pStream = _groupStreams[gdx][i];
				if (pStream && pStream->IsIdleState() == false) {
					int idx = -1;
					for (int j = 0; j < camLists.size(); j++) {
						if (pStream->channelSeq() == camLists[j]) {
							pStream->SetPTZLock(true);
							idx = j;
							cout << "channelSeq() : " << pStream->channelSeq() << ", camLists[j] : " << camLists[j] << endl;
							break;
						}
					}
					if (idx < 0) {
						pStream->SetPTZLock(false);
					}
				}
			}
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

#if 0
std::vector<int> IoS::GetLockLists() const
{
	std::vector<int> Lists;

	//std::string query = "select camera_seq from \"PTZ_LOCK\" where del_flag='N' and alive_time > now()";
	std::string query = "select camera_seq from \"PTZ_LOCK\" where del_flag='N'";

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	if (!_pPTZDBPostgre)
		return Lists;

	CString sResult = _pPTZDBPostgre->dbSelect(0, query);
	if (sResult.GetLength() <= DEF_CHECK_DB_RESULT_CNT)
		return Lists;

	AfxExtractSubString(sTok, sResult, 0, ',');
	sCount = sTok;

//	AfxExtractSubString(sTok, sResult, 1, '#');
	MakeExtractSubString(sTok, sResult, 1, _T("▶"));
	sWholeRecord = sTok;

	int count = _ttoi(sCount);
	for (int i = 0; i < count; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			return Lists;

		if (!AfxExtractSubString(sTok, sSingleRecord, 0, '$'))
			return Lists;

		Lists.push_back(_ttoi(sTok));
	}

	return Lists;
}
#else

std::vector<int> IoS::GetLockLists() const
{
	std::vector<int> Lists;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return Lists;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return Lists;
		}
	}
	std::string query = "select camera_seq from \"PTZ_LOCK\" where del_flag='N' and alive_time > now()";
	//std::string query = "select camera_seq from \"PTZ_LOCK\" where del_flag='N'";

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;

	CString sResult = dbPostgre.dbSelect(0, query);
	if (sResult.GetLength() <= 3) {
		dbPostgre._pDB->CloseConnection();
		return Lists;
	}

	dbPostgre._pDB->CloseConnection();

	AfxExtractSubString(sTok, sResult, 0, ',');
	sCount = sTok;

	//	AfxExtractSubString(sTok, sResult, 1, '#');
	MakeExtractSubString(sTok, sResult, 1, _T("▶"));
	sWholeRecord = sTok;

	int count = _ttoi(sCount);
	for (int i = 0; i < count; i++) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			return Lists;

		if (!AfxExtractSubString(sTok, sSingleRecord, 0, '$'))
			return Lists;

		Lists.push_back(_ttoi(sTok));
	}

	return Lists;
}
#endif

void IoS::StartLinkThread()
{
	if (g_bMaster == true)
		return;

	_bLink = true;
	_th_link = std::thread{ &IoS::LinkThread, this };
}

void IoS::StopLinkThread()
{
	if (_bLink) {
		_bLink = false;
		_th_link.join();
	}
}

void IoS::LinkThread()		//IOS, Master - Slave 통신 tcp 소켓 생성
{
	SOCKET sock = INVALID_SOCKET;
	bool bConnected = false;		//Master연결 bool 변수
	int ret = -1;

	std::unique_lock<std::mutex> lock(g_IoSMtx);
	int index = g_nMasterInx;
	lock.unlock();

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		return;

	int count = 0;
	do {
		if (Connect(sock, (char*)g_IoSLists[index].c_str(), 9006, 1) == true) {
			std::cout << "connect = " << std::to_string(index) << std::endl;
			bConnected = true;
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	} while (++count < 2);

	if (bConnected == false) {
		closesocket(sock);
		sock = INVALID_SOCKET;
		index = (index + 1) % g_IoSLists.size();
		std::unique_lock<std::mutex> lock(g_IoSMtx);
		g_nMasterInx = index;
		if (g_nMasterInx == g_nIoSInx) {
			std::cout << "Master2" << std::endl;		//처음 시작했을때 master가 죽어있어서, slave가 master로 바뀐 상황
			//if(IoSsetcheck(g_IoSLists[g_nPreMasterInx])==true)	UpdateIOSState(g_IoSLists[g_nPreMasterInx], false);		//kimdh0901 - ios고장처리
			ChangeMaster(index);
			g_AIMtx.lock();

			for (int i = 0; i < g_AICons.size() - 1; i++) {
				if (g_AIRecvCons[i] < MAX_CONS_PER_AI) {
					_mtxMaster.lock();
					UpdateClientConnection(i);
					RestartClientConnection(i);
					_mtxMaster.unlock();
				}
			}

			g_AIMtx.unlock();

			g_bMaster = true;
			g_nPreMasterInx = g_nMasterInx;
			return;
		}
		else {
			std::cout << "change index = " << std::to_string(index) << std::endl;
			g_bMaster = false;
		}
	}

	while (_bLink) {
		if (sock == INVALID_SOCKET) {
			sock = socket(AF_INET, SOCK_STREAM, 0);
			if (sock == INVALID_SOCKET)
				return;
		}

		if (bConnected == false) {
			std::cout << "try = " << std::to_string(index) << std::endl;
			if (Connect(sock, (char*)g_IoSLists[index].c_str(), 9006, 1) == false) {
				closesocket(sock);
				sock = INVALID_SOCKET;
				index = (index + 1) % g_IoSLists.size();
				std::unique_lock<std::mutex> lock(g_IoSMtx);
				g_nMasterInx = index;
				if (g_nMasterInx == g_nIoSInx) {
					std::cout << "Master3" << std::endl;		//운영 중에 Master가 죽어서 slave가 master로 바뀐 상황
					//if (IoSsetcheck(g_IoSLists[g_nPreMasterInx]) == true)	UpdateIOSState(g_IoSLists[g_nPreMasterInx], false);		//kimdh0901 - ios고장처리
					ChangeMaster(index);
					g_AIMtx.lock();

					for (int i = 0; i < g_AICons.size() - 1; i++) {
						if (g_AIRecvCons[i] < MAX_CONS_PER_AI) {
							_mtxMaster.lock();
							UpdateClientConnection(i);
							RestartClientConnection(i);
							_mtxMaster.unlock();
						}
					}

					g_AIMtx.unlock();

					g_bMaster = true;
					g_nPreMasterInx = g_nMasterInx;
					return;
				}
				else {
					std::cout << "change index = " << std::to_string(index) << std::endl;
					g_bMaster = false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			std::cout << "connect = " << std::to_string(index) << std::endl;
			bConnected = true;
		}

		TTCPPacketHeader header;
		ret = recv(sock, (char*)&header, sizeof(TTCPPacketHeader), 0);
		if (ret != sizeof(header)) {
			closesocket(sock);
			sock = INVALID_SOCKET;
			bConnected = false;
			index = (index + 1) % g_IoSLists.size();
			std::unique_lock<std::mutex> lock(g_IoSMtx);
			g_nMasterInx = index;
			if (g_nMasterInx == g_nIoSInx) {
				std::cout << "Master4" << std::endl;		//master와 연결은 하고있는데, Master가 recv값이 규정에 맞지 않는 값을 보내면 Master가 이상하거나 죽었다고 판단, slave가 Master로 바뀐 상황
				//if (IoSsetcheck(g_IoSLists[g_nPreMasterInx]) == true)	UpdateIOSState(g_IoSLists[g_nPreMasterInx], false);		//kimdh0901 - ios고장처리
				ChangeMaster(index);
				g_AIMtx.lock();

				for (int i = 0; i < g_AICons.size() - 1; i++) {
					if (g_AIRecvCons[i] < MAX_CONS_PER_AI) {
						_mtxMaster.lock();
						UpdateClientConnection(i);
						RestartClientConnection(i);
						_mtxMaster.unlock();
					}
				}

				g_AIMtx.unlock();

				g_bMaster = true;
				g_nPreMasterInx = g_nMasterInx;
				return;
			}
			else {
				std::cout << "change index = " << std::to_string(index) << std::endl;
				g_bMaster = false;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

bool IoS::ChangeMaster(int index)
{
	if (_evtIP.empty())
		return false;

	if (index < 0 || index >= g_IoSLists.size())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (!dbPostgre._pDB) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	//std::string query = std::string("UPDATE \"CURATOR_CONNECTION\" SET master_gb = '0' where master_gb = '1'; UPDATE \"CURATOR_CONNECTION\" SET master_gb = '1' where curator_ip = '") + g_IoSLists[index] + std::string("'");
	std::string query = std::string("UPDATE SVR_INFO_CLAS SET master_gb = CASE WHEN svr_info_seq IN (SELECT svr_info_seq FROM SVR_INFO WHERE svr_ip='") + g_IoSLists[index] + std::string("' AND del_yn = 0) THEN 1 ELSE 0 END");		//kimdh0714 - chagemasterquery
	dbPostgre.dbUpdate(query);
	dbPostgre._pDB->CloseConnection();

	return true;
}

bool IoS::Connect(SOCKET sock, char* host, int port, int timeout)
{
	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, host, &addr.sin_addr);

	ULONG nonBlk = TRUE;
	if (ioctlsocket(sock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
		return false;
	}

	int nRet = ::connect(sock, (const struct sockaddr*)&addr, sizeof(addr));
	if (nRet == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		return false;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET((unsigned int)sock, &fdset);

	struct timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	if (select(0, NULL, &fdset, NULL, &tv) == SOCKET_ERROR) {
		return false;
	}

	if (!FD_ISSET(sock, &fdset)) {
		return false;
	}

	nonBlk = FALSE;
	if (ioctlsocket(sock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
		return false;
	}

	return true;
}

bool IoS::startup(int port, int port_mas, int port_link)
{
	if (_server_link->startup(2, port_link)) {
		if (_server_mas->startup(1, port_mas)) {
			if (_server->startup(0, port)) {
				return true;
			}
			else {
				_server_mas->shutdown();
			}
		}
		else {
			_server_link->shutdown();
		}
	}

	return false;
}

void IoS::shutdown()
{
	_server->shutdown();
	_server_mas->shutdown();
	_server_link->shutdown();
}

void IoS::onServerSessionOpened(TCPSession* session)
{
}

void IoS::onServerSessionClosed(TCPSession* session)
{
	if (session->_type == 1) {
		g_AIMtx.lock();
		for (int i = 0; i < g_AICons.size() - 1; i++) {
			uint8_t val = g_IoSCons[make_pair(inet_ntoa(session->sockaddr_in()->sin_addr), i)];
			if (val > 0) {
				g_IoSCons[make_pair(inet_ntoa(session->sockaddr_in()->sin_addr), i)] = 0;
				if (g_AICons[i] >= val)
					g_AICons[i] -= val;
				else
					g_AICons[i] = 0;

				std::cout << "send2-1[" << std::to_string(i) << "] = " << std::to_string(g_AICons[i]) << ", " << std::to_string(g_AIRecvCons[i]) << std::endl;
//				cout << "send2[" << std::to_string(i) << "] = " << std::to_string(g_AICons[i]) << std::endl;
			}
		}
		g_AIMtx.unlock();
	}
}

void IoS::onRecvData(TCPSession* session, const uint8_t *data, int size)
{
	if (session->_type == 0) {
		std::unique_lock<std::mutex> lock(_mtxMsg);

		CString msgData;
		char* rcv_data = new char[size + 1];
		::memcpy(rcv_data, data, size);
		rcv_data[size] = 0;

#if 1
		if (size < 16) {
			msgData = CA2T((char*)rcv_data);
			delete[] rcv_data;
		}
		else {
			BYTE pbszUserKey[16] = { 0x75, 0x62, 0x61, 0x79, 0x73, 0x6F, 0x6C, 0x5F, 0x69, 0x6F, 0x74, 0x5F, 0x70, 0x61, 0x73, 0x73 };
			BYTE pbszIV[16] = { 0x75, 0x62, 0x61, 0x79, 0x73, 0x6F, 0x6C, 0x5F, 0x69, 0x6F, 0x74, 0x5F, 0x76, 0x65, 0x63, 0x74 };

			int nPlainTextLen;
			int nCipherTextLen;

			size_t out_len = b64_decoded_size(rcv_data) + 1;
			char* out = new char[out_len];
			::memset(out, 0x00, out_len);

			b64_decode(rcv_data, (unsigned char*)out, out_len);
			delete[] rcv_data;

			nCipherTextLen = (((int)out_len - 1) + 15) / 16 * 16;

			uint8_t* pbszPlainText = new uint8_t[nCipherTextLen];
			::memset(pbszPlainText, 0x00, nCipherTextLen);
			nPlainTextLen = SEED_CBC_Decrypt((unsigned char*)pbszUserKey, (unsigned char*)pbszIV, (uint8_t*)out, nCipherTextLen, pbszPlainText);
			delete[] out;

			msgData = CA2T((char*)pbszPlainText);
			delete[] pbszPlainText;
		}
#else
		msgData = CA2T((char*)rcv_data);
#endif
		std::cout << "cmd = " << std::string(CT2CA(msgData)) << std::endl;

		CString msgNum, msgCmd;
		::AfxExtractSubString(msgNum, msgData, 0, '$');
		int num = _ttoi(msgNum);
		if (num <= 0)
			return;

		::AfxExtractSubString(msgCmd, msgData, 1, '$');
		if (msgCmd.Compare(_T("00")) == 0) {					// System Start All
			if (num < 3)
				return;
			if (g_bStartSystem == false) {
				CString msgIP1, msgIP2;
				::AfxExtractSubString(msgIP1, msgData, 2, '$');
				::AfxExtractSubString(msgIP2, msgData, 3, '$');

				StartSystem(std::string(CT2CA(msgIP1)), std::string(CT2CA(msgIP2)));
				g_bStartSystem = true;
			}
		}
		else if (msgCmd.Compare(_T("01")) == 0) {			// System Stop All
			if (g_bStartSystem) {
				UpdateProcess(0, 2);
				StopAnalyses();
				g_bAlgorithm = false;
				g_bStartSystem = false;
			}
		}
		else if (msgCmd.Compare(_T("02")) == 0) {			// Alg Start All
			if (g_bStartSystem) {
				if (g_bAlgorithm == false) {
					UpdateProcess(2, 1);
					StartAlgAll();
					g_bAlgorithm = true;
				}
			}
		}
		else if (msgCmd.Compare(_T("03")) == 0) {			// Alg Stop All
			if (g_bStartSystem) {
				UpdateProcess(2, 2);
				StopAlgAll();
				g_bAlgorithm = false;
			}
		}
		else if (msgCmd.Compare(_T("04")) == 0) {			// Alg or ROI or Channel
			if (g_bStartSystem) {
				if (num <= 1) {
					UpdateProcess(4, -1);
					/*
					g_bAlgorithm = false;
					g_bStartSystem = false;
					Sleep(100);

					ApplyAlgAll();

					g_bStartSystem = true;
					g_bAlgorithm = true;
					UpdateProcess(5, -1);
					*/
					ShellExecuteW(NULL, _T("open"), _T("D:\\Curation\\app\\restart.bat"), NULL, NULL, SW_SHOW);
				}
//				else {
//					CString msgCam;
//					::AfxExtractSubString(msgCam, msgData, 2, '$');
//
//					int cam_seq = _ttoi(msgCam);
//					ApplyAlgCamOpt(cam_seq);
//				}
			}
		}
		else if (msgCmd.Compare(_T("05")) == 0) {			// Alg Start
			if (num <= 1)
				return;

			if (g_bStartSystem) {
				CString msgCamList;
				::AfxExtractSubString(msgCamList, msgData, 2, '$');

				std::string camList = std::string(CT2CA(msgCamList));
				StartAlgCamera(camList);
			}
		}
		else if (msgCmd.Compare(_T("06")) == 0) {			// Alg Stop
			if (num <= 1)
				return;

			if (g_bStartSystem) {
				CString msgCamList;
				::AfxExtractSubString(msgCamList, msgData, 2, '$');

				std::string camList = std::string(CT2CA(msgCamList));
				StopAlgCamera(camList);
			}
		}
		else if (msgCmd.Compare(_T("07")) == 0) {			// 카메라 추가
			if (num <= 1)
				return;

			if (g_bStartSystem) {
				CString msgCamList;
				::AfxExtractSubString(msgCamList, msgData, 2, '$');

				std::string camList = std::string(CT2CA(msgCamList));
				AddCamera(camList, 1);
			}
		}
		else if (msgCmd.Compare(_T("08")) == 0) {			// 카메라 삭제
			if (num <= 1)
				return;

			if (g_bStartSystem) {
				CString msgCamList;
				::AfxExtractSubString(msgCamList, msgData, 2, '$');

				std::string camList = std::string(CT2CA(msgCamList));
				RemoveCamera(camList, 1);
			}
		}
		else if (msgCmd.Compare(_T("09")) == 0) {			// 카메라 추가 삭제
			if (num <= 1)
				return;

			if (g_bStartSystem) {
				CString msgCamList;
				::AfxExtractSubString(msgCamList, msgData, 2, '$');
				CString msgRemoveList;
				::AfxExtractSubString(msgRemoveList, msgCamList, 1, '^');
				if (msgRemoveList.IsEmpty() == false) {
					std::string camList = std::string(CT2CA(msgRemoveList));
					RemoveCamera(camList);
				}
				CString msgAddList;
				::AfxExtractSubString(msgAddList, msgCamList, 0, '^');
				if (msgAddList.IsEmpty() == false) {
					std::string camList = std::string(CT2CA(msgAddList));
					AddCamera(camList);
				}
			}
		}
		else if (msgCmd.Compare(_T("15")) == 0) {
			if (g_bStartSystem)
				EnableCameraState();
		}
		else if (msgCmd.Compare(_T("17")) == 0) {
			if (num <= 1)
				return;

			CString msgCam;
			::AfxExtractSubString(msgCam, msgData, 2, '$');

			int cam_seq = _ttoi(msgCam);
			GenerateStillImage(cam_seq);
		}
	}
	else if (session->_type == 1) {
		if (size < sizeof(CmdHeaderII)) {
			return;
		}

		bool bMaster = true;

		std::unique_lock<std::mutex> lock(g_IoSMtx);
		bMaster = g_bMaster;
		lock.unlock();

		CmdHeaderII ReplyHeader;
		::memset(&ReplyHeader, 0x00, sizeof(CmdHeaderII));

		if (bMaster) {
			CmdHeaderII Header;
			::memcpy(&Header, data, sizeof(CmdHeaderII));

			ReplyHeader.cmd = Header.cmd;

			if (Header.cmd == 0) {
				int index = -1;
				g_AIMtx.lock();

				if (g_AICons.size() > 1) {
					int cnt = g_AICons[0];
					index = 0;
					for (int i = 1; i < g_AICons.size() - 1; i++) {
						if (cnt > g_AICons[i]) {
							cnt = g_AICons[i];
							index = i;
						}
					}

					if (cnt >= MAX_CONS_PER_AI) {
						index = -1;
					}
					else {
						++g_AICons[index];
						++g_IoSCons[make_pair(inet_ntoa(session->sockaddr_in()->sin_addr), index)];
						std::cout << "send2-2[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << ", " << std::to_string(g_AIRecvCons[index]) << std::endl;
//						std::cout << "send2[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << std::endl;
					}
				}
				g_AIMtx.unlock();

				ReplyHeader.reply = 0;
				ReplyHeader.data = index + 1;
			}
			else if (Header.cmd == 1) {
				g_AIMtx.lock();
				if (g_AICons.size() <= 1) {
					ReplyHeader.reply = 1;
				}
				else {
					int index = Header.category;
					if (index < 1 || index >= g_AICons.size()) {
						ReplyHeader.reply = 1;
					}
					else {
						index--;
						if (g_AICons[index] > 0 && g_AICons[index] <= MAX_CONS_PER_AI) {
							--g_AICons[index];
							--g_IoSCons[make_pair(inet_ntoa(session->sockaddr_in()->sin_addr), index)];
							std::cout << "send2-3[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << ", " << std::to_string(g_AIRecvCons[index]) << std::endl;
//							std::cout << "send2[" << std::to_string(index) << "] = " << std::to_string(g_AICons[index]) << std::endl;
						}
						else {
							ReplyHeader.reply = 1;
						}
					}
				}
				g_AIMtx.unlock();
			}
		}
		else {
			ReplyHeader.reply = 255;
		}

		session->send((uint8_t*)&ReplyHeader, sizeof(CmdHeaderII));
	}
}

void IoS::GenerateStillImage(int ch_seq)
{
	if (ch_seq < 0)
		return;

	CDBPostgre dbPostgre(_evtIP);
	if (dbPostgre._pDB == NULL) {
		return;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return;
		}
	}

	char buffer[128];
	::sprintf(buffer, "SELECT rtsp_address, id, pw FROM \"CAMERAS\" where cameras_seq='%d'", ch_seq);
	std::string query(buffer);

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= 3) {
		dbPostgre._pDB->CloseConnection();
		return;
	}

	dbPostgre._pDB->CloseConnection();

	CString strDiv, strAddress, strId, strPass;
//	::AfxExtractSubString(strDiv, strResult, 1, '#');
	::MakeExtractSubString(strDiv, strResult, 1, _T("▶"));
	::AfxExtractSubString(strAddress, strDiv, 0, '$');
	::AfxExtractSubString(strId, strDiv, 1, '$');
	::AfxExtractSubString(strPass, strDiv, 2, '$');

	CString param;
	if (strAddress.IsEmpty())
		param = _T("''");
	else
		param = strAddress;

	param += _T(" ");

	if (strId.IsEmpty())
		param += _T("''");
	else
		param += strId;

	param += _T(" ");

	if (strPass.IsEmpty())
		param += _T("''");
	else
		param += strPass;

	param += _T(" ") + CString(std::to_string(ch_seq).c_str());

	//	CP(L"param = %s\n", param);

	::ShellExecute(::GetDesktopWindow(), _T("open"), L"GenerateStillImage.exe", param, 0, SW_SHOWDEFAULT);
}

bool IoS::GetIoSName()
{
	if (_curIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (dbPostgre._pDB == NULL) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = "SELECT curator_name, curator_seq FROM \"CURATOR_CONNECTION\" where curator_ip='" + _curIP + "'";

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= 3) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString strDiv1, strDiv2;
//	::AfxExtractSubString(strDiv1, strResult, 1, '#');
	::MakeExtractSubString(strDiv1, strResult, 1, _T("▶"));
	::AfxExtractSubString(strDiv2, strDiv1, 0, '$');
	g_strIoSName = strDiv2;
	::AfxExtractSubString(strDiv2, strDiv1, 1, '$');
	g_strIoSSeq = strDiv2;
	cout << "g_strIoSName : " << CT2CA(g_strIoSName) << endl;
	cout << "g_strIoSSeq : " << CT2CA(g_strIoSSeq) << endl;
	return true;
}

bool IoS::UpdateAIState(int index, bool bState)		//kimdh0721 - R2 고장처리
{
	if (index < 0 || index >= g_AIIPs.size())
		return false;

	if (bState == false) {
		CString strName;
		std::string sIP;
		if (index == g_AIIPs.size() - 1) {
			strName = L"Inner R2";
			sIP = _curIP;
		}
		else {
			strName = g_AINames[index];
			sIP = g_AIIPs[index];
			if (g_bMaster != true) return false;
		}

		std::string query = "INSERT INTO device_err_mgr(device_type, device_nm, device_ip, detct_device_nm, detct_device_ip, err_code, err_msg) VALUES('iot', '" + std::string(CT2CA(strName)) + "', '" + sIP + "', '" + std::string(CT2CA(g_strIoSName)) + "', '" + _curIP + "', '";

		CString strComment = L"NC', '연결 실패')";

		query += std::string(CT2CA(strComment));

		if (_evtIP.empty())
			return false;

		CDBPostgre* p_DbPostgre = new CDBPostgre(_evtIP);
		if (!p_DbPostgre)
			return false;

		if (!p_DbPostgre->_pDB) {
			delete p_DbPostgre;
			return false;
		}
		else {
			ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
			if (connstatus == CONNECTION_BAD) {
				p_DbPostgre->_pDB->CloseConnection();
				delete p_DbPostgre;
				return false;
			}
		}
		p_DbPostgre->dbInsert(query);

		p_DbPostgre->_pDB->CloseConnection();

		delete p_DbPostgre;
	}

	return true;
}
bool IoS::UpdateIOSState(string IoSIP, bool bState)		//kimdh0901 - IoS고장처리
{
	bool overindex = true;
	int index = 0;

	for (index; index < g_IoSLists.size(); index++) {
		if (g_IoSLists[index] == IoSIP) {
			overindex = false;
			break;
		}
	}
	if (overindex) return false;

	if (bState == false) {
		CString strName;
		std::string sIP;

		strName = g_IoSNames[index];
		sIP = g_IoSLists[index];

		std::string query = "INSERT INTO device_err_mgr(device_type, device_nm, device_ip, detct_device_nm, detct_device_ip, err_code, err_msg) VALUES('ios', '" + std::string(CT2CA(strName)) + "', '" + sIP + "', '" + std::string(CT2CA(g_strIoSName)) + "', '" + _curIP + "', '";

		CString strComment = L"NC', '연결 실패')";

		query += std::string(CT2CA(strComment));

		if (_evtIP.empty())
			return false;

		CDBPostgre* p_DbPostgre = new CDBPostgre(_evtIP);
		if (!p_DbPostgre)
			return false;

		if (!p_DbPostgre->_pDB) {
			delete p_DbPostgre;
			return false;
		}
		else {
			ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
			if (connstatus == CONNECTION_BAD) {
				p_DbPostgre->_pDB->CloseConnection();
				delete p_DbPostgre;
				return false;
			}
		}

		p_DbPostgre->dbInsert(query);

		p_DbPostgre->_pDB->CloseConnection();

		delete p_DbPostgre;
	}

	return true;
}

bool IoS::UpdateErrorMgr(string CurIP)		//kimdh0813 - 고장처리
{

	std::string query = "UPDATE svr_info set err_stat = 0 where svr_ip = '" + CurIP + "'";

	if (_evtIP.empty())
		return false;

	CDBPostgre* p_DbPostgre = new CDBPostgre(_evtIP);
	if (!p_DbPostgre)
		return false;

	if (!p_DbPostgre->_pDB) {
		delete p_DbPostgre;
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			p_DbPostgre->_pDB->CloseConnection();
			delete p_DbPostgre;
			return false;
		}
	}

	p_DbPostgre->dbInsert(query);

	p_DbPostgre->_pDB->CloseConnection();

	delete p_DbPostgre;


	return true;
}

bool IoS::IoSsetcheck(string IoSIP)
{
	if (_curIP.empty())
		return false;

	CDBPostgre dbPostgre(_evtIP);
	if (dbPostgre._pDB == NULL) {
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(dbPostgre._pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			dbPostgre._pDB->CloseConnection();
			return false;
		}
	}

	std::string query = "SELECT send_config, op_config FROM \"CURATION_PROCESS\" where svr_ip='" + IoSIP + "'";

	CString strResult = dbPostgre.dbSelect(0, query);
	if (strResult.GetLength() <= 3) {
		dbPostgre._pDB->CloseConnection();
		return false;
	}

	dbPostgre._pDB->CloseConnection();

	CString strDiv1, send_conf, op_conf;
	//	::AfxExtractSubString(strDiv1, strResult, 1, '#');
	::MakeExtractSubString(strDiv1, strResult, 1, _T("▶"));
	::AfxExtractSubString(send_conf, strDiv1, 0, '$');
	::AfxExtractSubString(op_conf, strDiv1, 1, '$');

	if (send_conf == op_conf) return true;
	else return false;
}

void IoS::StartConCheckThread()
{
	_bLoopCheck = true;
	_th_check = std::thread{ &IoS::ConCheckThread,this };
}

void IoS::StopConCheckThread()
{
	if (_bLoopCheck == true)
	{
		_bLoopCheck = false;
		_th_check.join();
	}
}

void IoS::ConCheckThread()
{
	SOCKET sock = INVALID_SOCKET;
	bool bConnected = false;
	bool firstSend = false;

	while (_bLoopCheck)
	{
		int retval = -1;

		sock = socket(AF_INET, SOCK_STREAM, 0);

		if (sock == INVALID_SOCKET) {
			bConnected = false;
			sock = NULL;

			closesocket(sock);
			continue;
		}

		if (Connect(sock, (char*)_evtIP.c_str(), 9918, 5) != true)
		{
			cout << "Keep Alive Connect Err\n";
			bConnected = false;
			sock = NULL;

			closesocket(sock);
		}
		else
		{
			cout << "Keep Alive Connect\n";
			firstSend = true;
			bConnected = true;
		}

		std::chrono::time_point<std::chrono::steady_clock> st = std::chrono::steady_clock::now();

		while (bConnected == true)
		{
			//5초
			//std::this_thread::sleep_for(std::chrono::seconds(5));
			std::chrono::time_point<std::chrono::steady_clock> et = std::chrono::steady_clock::now(); //현재시간
			std::chrono::duration<double> spent = et - st;
			if ((int)spent.count() >= 20 || firstSend)
			{
				firstSend = false;
				st = std::chrono::steady_clock::now();
				Json::Value data;

				string text = "ios_";
				string str = _curIP.substr(_curIP.rfind(".") + 1, _curIP.length() - 1);
				text += str;
				data["svr_ip"] = _curIP;
				data["svr_info_seq"] = std::string(CT2CA(g_strIoSSeq));
				data["svr_type"] = text;
				//data["svr_type"] = "CL";

				Json::StreamWriterBuilder builder;
				std::string cmds = Json::writeString(builder, data);
				//cout << cmds << endl;
				retval = send(sock, cmds.c_str(), cmds.length(), 0);

				if (retval == -1) //실패시
				{
					bConnected = false;
					sock = NULL;

					closesocket(sock);

					time_t curTime = time(NULL);
					struct tm* pLocal = localtime(&curTime);

					printf("%02d:%02d:%02d", pLocal->tm_hour, pLocal->tm_min, pLocal->tm_sec);
					cout << " Send Err ::: " << _curIP << " ==> " << _evtIP << endl;
					firstSend = false;
				}
			}

		}
	}
}

bool ConnectToServer(SOCKET& sock, const std::string& ip, int port) {
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) {
		std::cerr << "[Error] 소켓 생성 실패\n";
		return false;
	}

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

	if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		std::cerr << "[Error] TCP 연결 실패, 5초 후 재시도...\n";
		closesocket(sock);
		sock = INVALID_SOCKET;
		return false;
	}
	std::cout << "[Info] TCP 연결 성공 (" << ip << ":" << port << ")\n";
	return true;
}

void GlobalCoordSendThread(IoS* iosObj) {
	const auto& connInfo = iosObj->GetConnectInfo();

	while (true) {
		SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET) {
			std::cerr << "[Error] 소켓 생성 실패\n";
			std::this_thread::sleep_for(std::chrono::seconds(3));
			continue;
		}

		sockaddr_in serverAddr;
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(9915);
		inet_pton(AF_INET, "192.168.0.53", &serverAddr.sin_addr);

		if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
			std::cerr << "[Error] TCP 연결 실패. 재시도 중...\n";
			closesocket(sock);
			std::this_thread::sleep_for(std::chrono::seconds(3));
			continue;
		}
		std::cout << "[Info] TCP 연결 성공 (192.168.0.53:9080)\n";

		while (true) {
			std::unique_lock<std::mutex> lock(g_CoordMtx);
			g_CoordCV.wait(lock, [] { return !g_CoordQueue.empty(); });

			while (!g_CoordQueue.empty()) {
				ObjectCoord coord = g_CoordQueue.front();
				g_CoordQueue.pop();
				lock.unlock();

				// 🌟 JSON 데이터 생성
				Json::Value root;
				root["event_area"] = "detection_zone";
				root["channel"] = coord.ch_seq;

				Json::Value coordinates(Json::arrayValue);
				coordinates.append(coord.x / 640.0);
				coordinates.append(coord.y / 480.0);
				coordinates.append((coord.x + coord.w) / 640.0);
				coordinates.append((coord.y + coord.h) / 480.0);

				root["coordinates"] = coordinates;

				// 🎯 coord.ch_seq와 일치하는 카메라 정보 찾기
				auto camIt = std::find_if(connInfo.Cameras.begin(), connInfo.Cameras.end(),
					[&coord](const ChInfo_t& cam) {
						return cam.ch_seq == coord.ch_seq;
					});

				if (camIt != connInfo.Cameras.end()) {
					// 🎥 카메라 정보 추가
					Json::Value camInfo;
					root["group_index"] = camIt->group_index;
					root["ch_seq"] = camIt->ch_seq;
					root["ch_id"] = camIt->ch_id;
					//root["camera_name"] = std::string(CT2CA(camIt->strChName));
					root["ip_address"] = camIt->ch_ip;
					root["is_ptz"] = camIt->is_ptz;

					Json::Value rtspArray(Json::arrayValue);
					for (const auto& rtsp : camIt->rtsp_address)
						rtspArray.append(rtsp);
					root["rtsp_address"] = rtspArray;

					root["login_id"] = camIt->login_id;
					root["login_pass"] = camIt->login_pass;
					root["alg_mode"] = camIt->alg_mode;
					root["status"] = camIt->status;

					Json::Value durTimesArray(Json::arrayValue);
					for (int t : camIt->durTimes)
						durTimesArray.append(t);
					root["duration_times"] = durTimesArray;
				}
				else {
					// ⚠️ 일치하는 카메라가 없을 때
					root["camera_info"]["error"] = "해당 SEQ의 카메라 정보 없음";
				}

				// 🌐 JSON 문자열 변환
				Json::StreamWriterBuilder writer;
				writer["indentation"] = "";
				std::string jsonData = Json::writeString(writer, root) + "\n";

				// 📡 데이터 전송
				if (send(sock, jsonData.c_str(), jsonData.size(), 0) == SOCKET_ERROR) {
					std::cerr << "[Error] 전송 실패. 재연결 시도.\n";
					closesocket(sock);
					break;
				}
				std::cout << "[전송된 JSON 데이터] " << jsonData;
				lock.lock();
			}
		}
		closesocket(sock);
	}
}




int main() {
	av_log_set_level(AV_LOG_QUIET);

	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	IoS manager;
	manager.startup(9001, 9000, 9006);

	// ✅ 📡 [좌표 전송 스레드 시작] 공용 큐에서 좌표값 가져와 TCP 전송
	std::thread coordSendThread(GlobalCoordSendThread, &manager);

	FILE* error_log = freopen("./IoS.log", "w", stderr);
	if (error_log == nullptr) {
		std::cerr << "Error opening log file!" << std::endl;
		return 1;
	}

	// 🌙 시스템이 계속 실행되도록 대기
	while (1)
		std::this_thread::sleep_for(std::chrono::seconds(1));

	// ⏹ [프로그램 종료 시 처리]
	manager.shutdown();
	WSACleanup();
	fclose(error_log);

	// ✋ 전송 스레드 종료 대기
	if (coordSendThread.joinable())
		coordSendThread.join();

	return 0;
}
