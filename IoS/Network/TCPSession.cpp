#include "pch.h"
#include "TCPSession.h"
#include "DataBaseManager.h"
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern CString g_strIoSName;
extern std::vector<std::string>	g_IoSLists;
extern std::vector<CString> g_IoSNames;
extern std::string _evtIP;
extern std::string _curIP;

TCPSession::TCPSession()
	: _sock(INVALID_SOCKET)
	, _listener(nullptr)
{
}

TCPSession::~TCPSession()
{
	close();
}

void TCPSession::open(SOCKET sock, SOCKADDR_IN& addr, ITCPSessionListener* listener)
{
	_sock = sock;
	_addr = addr;
	_parser.open(this);
	_listener = listener;

	thrStart();
}

void TCPSession::close()
{
	thrStop();

	_parser.close();
	_listener = nullptr;

	if (_sock != INVALID_SOCKET)
	{
		closesocket(_sock);
		_sock = INVALID_SOCKET;
	}
}

bool TCPSession::send(const uint8_t *data, int size)
{
	CMonitor::Owner lock(_lock);
	
	TTCPPacketHeader header;
	memcpy(&header.code, TCPSessionParser::PACKET_MAGIC_CODE, 4);
	header.contentLength = size;

	if (sendData((const uint8_t *)&header, sizeof(TTCPPacketHeader))
		&& sendData(data, size))
	{
		return true;
	}

	return false;
}

bool TCPSession::sendData(const uint8_t *data, int size)
{
	CMonitor::Owner lock(_lock);

	if (_sock != INVALID_SOCKET)
	{
		int offset = 0;

		while (1)
		{
			int nbytes = ::send(_sock, (const char *)data + offset, size - offset, 0);
			if (nbytes > 0)
			{
				offset += nbytes;
				if (offset == size) {
					return true;
				}
			}
			else if (WSAGetLastError() != WSAEWOULDBLOCK)
			{
				thrStop(1);
				break;
			}
		}
	}

	return false;
}

void TCPSession::onRecvData(const uint8_t *data, int size)
{
	if (_listener)
		_listener->onRecvData(this, data, size);
}

void TCPSession::threadProc()
{
	uint8_t* recvBuffer = new uint8_t[RECV_BUFFER_SIZE];
	int offset = 0;

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1;

	ULONG nonBlk = TRUE;
	ioctlsocket(_sock, FIONBIO, &nonBlk);
	
	while (CThread::thrRunning())
	{
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(_sock, &fdset);

		if (select(0, &fdset, nullptr, nullptr, &tv) != SOCKET_ERROR)
		{
			if (FD_ISSET(_sock, &fdset))
			{
				if (offset == RECV_BUFFER_SIZE)
					offset = 0;

				int nbytes = recv(_sock, (char*)recvBuffer + offset, RECV_BUFFER_SIZE - offset, 0);			
				if (nbytes == -1) {
					//if(IoSsetcheck(inet_ntoa(this->_addr.sin_addr))==true)	UpdateIOSState(inet_ntoa(this->_addr.sin_addr), false);
				}

				if (nbytes > 0)
				{
					int recvBufferSize = nbytes + offset;
					int remain = _parser.feed(recvBuffer, recvBufferSize);
					if (remain == -1) {
						break;
					}

					offset = remain;

					if (remain > 0)
						memcpy(recvBuffer, recvBuffer + (recvBufferSize - remain), remain);
				}
				else if (WSAGetLastError() != WSAEWOULDBLOCK)
				{					
					break;
				}
			}
		}
	}

	delete[] recvBuffer;

	if (_listener)
		_listener->onDisconnected(this);
}

bool TCPSession::UpdateIOSState(string IoSIP, bool bState)		//kimdh0901 - IoS고장처리
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

bool TCPSession::IoSsetcheck(string IoSIP)
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