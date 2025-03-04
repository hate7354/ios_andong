#include "pch.h"
#include "QueryDB.h"


CQueryDB::CQueryDB(std::string &host)
{
	m_conn = nullptr;
	res = nullptr;

	CreateQueryDB(host);
}


CQueryDB::~CQueryDB()
{
}


bool CQueryDB::CreateQueryDB(std::string &host) {
	if (host.empty())
		return false;

	try {
		m_conn = PQsetdbLogin(host.c_str(), DEF_DB_SERVER_PORT, nullptr, nullptr, DEF_DB_SERVER_NAME, DEF_DB_SERVER_USER, DEF_DB_SERVER_PASS);
		ConnStatusType connstatus = PQstatus(m_conn);
		if (connstatus == CONNECTION_BAD)
		{
			return false;
		}

		PQsetClientEncoding(m_conn, "UHC");
	}
	catch(...) {

	}

	return true;
}


PGconn* CQueryDB::GetConnection() {

	return m_conn;
}

BOOL CQueryDB::Select(std::string sQuerty) {

	res = PQexec(m_conn, sQuerty.c_str());
	row_num = PQntuples(res);

	ExecStatusType retStatus = PQresultStatus(res);
	if (retStatus == ExecStatusType::PGRES_TUPLES_OK ||
		retStatus == ExecStatusType::PGRES_COMMAND_OK)
	{
		return TRUE;
	}

	CString errStr;
	errStr = PQerrorMessage(m_conn);

	return FALSE;
}

BOOL CQueryDB::Insert(std::string sQuerty) {

	res = PQexec(m_conn, sQuerty.c_str());
	
	//PGRES_TUPLES_OK : Query가 성공적으로 수행되었을 경우
	//PGRES_COMMAND_OK : 서버로 명령(접속 명령어등)가 성공적으로 실행되고, 그 결과로 돌아오는 tuple이 없을 경우
	ExecStatusType retStatus = PQresultStatus(res);
	if (retStatus == ExecStatusType::PGRES_TUPLES_OK ||
		retStatus == ExecStatusType::PGRES_COMMAND_OK)
	{
		return TRUE;
	}

	CString errStr;
	errStr = PQerrorMessage(m_conn);

	return FALSE;
}

BOOL CQueryDB::Update(std::string querty) {

	res = PQexec(m_conn, querty.c_str());

	ExecStatusType retStatus = PQresultStatus(res);
	if (retStatus == ExecStatusType::PGRES_TUPLES_OK ||
		retStatus == ExecStatusType::PGRES_COMMAND_OK)
	{
		return TRUE;
	}

	CString errStr;
	errStr = PQerrorMessage(m_conn);
	
	return FALSE;
}

BOOL CQueryDB::Delete(std::string querty) {

	res = PQexec(m_conn, querty.c_str());
	ExecStatusType retStatus = PQresultStatus(res);
	if (retStatus == ExecStatusType::PGRES_TUPLES_OK ||
		retStatus == ExecStatusType::PGRES_COMMAND_OK)
	{
		return TRUE;
	}

	CString errStr;
	errStr = PQerrorMessage(m_conn);

	return FALSE;
}

//postgresql function call
BOOL CQueryDB::FunctionExecute(std::string querty, int paramCount, const char* const* paramValues)
{
	res = PQexecParams(
		m_conn,			//conn
		querty.c_str(),	//command
		paramCount,		//param count
		nullptr,			//let the backend deduce param type (param type)
		paramValues,	//param values
		nullptr,			//don't need param lengths since text (param length)
		nullptr,			//default to all text params, nullptr인 경우 모든 파라미터는 텍스트 문자열로 가정
		0				//ask for text, binary results (0:결과를 텍스트로, 1:결과를 바이너리로)
	);

	ExecStatusType retStatus = PQresultStatus(res);
	if (retStatus == ExecStatusType::PGRES_TUPLES_OK ||
		retStatus == ExecStatusType::PGRES_COMMAND_OK)
	{
		return TRUE;
	}

	CString errStr;
	errStr = PQerrorMessage(m_conn);

	return FALSE;
}

int CQueryDB::ValueInt(int row, int col) {
	return _ttoi((CString)PQgetvalue(res, row, col));
}

CString CQueryDB::ValueString(int row, int col) {
	return (CString)PQgetvalue(res, row, col);
}

CStringA CQueryDB::ValueStringA(int row, int col) {
	return PQgetvalue(res, row, col);
}


int CQueryDB::ValueInt(int col) {
	return _ttoi((CString)PQgetvalue(res, res_index, col));
}

CString  CQueryDB::ValueString(int col) {

	CString sz = (CString)PQgetvalue(res, res_index, col);
 	return sz;
}

CStringA  CQueryDB::ValueStringA(int col) {
	return PQgetvalue(res, res_index, col);
}


BOOL CQueryDB::NextRow()
{
	BOOL result = TRUE;
	res_index++;
	if (res_index + 1 > row_num) {
		res_index = row_num;
		result = FALSE;
	}

	return result;
}

int CQueryDB::SetFirstRow()
{
	res_index = -1;
	return res_index;
}


int CQueryDB::SetLastRow()
{
	res_index = row_num;
	return res_index;
}


void CQueryDB::CloseConnection() {
	if (res) {
		PQclear(res);
		res = nullptr;
	}
	if (m_conn) {
		PQfinish(m_conn);
		m_conn = nullptr;
	}
	delete this;
}
