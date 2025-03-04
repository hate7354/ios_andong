#include "pch.h"
#include "DBPostgre.h"


CDBPostgre::CDBPostgre(std::string &host) : _pDB(nullptr)
{
	DBprocessCreate(host);
}

CDBPostgre::~CDBPostgre()
{
}


bool CDBPostgre::DBprocessCreate(std::string &host)
{
	_pDB = new CQueryDB(host);

	if (!_pDB)
		return false;

	return true;
}


CString CDBPostgre::dbSelect(int nidx, std::string sQueryString)
{
	if (!_pDB) return _T("");

	_pDB->res_index = 0;
	_pDB->Select(sQueryString);

	if (_pDB->row_num <= 0) return _T("");

	CString strTemp = _T("");

	int whatCol = 0;
	int whatRow = 0;
	bool tofCnt = false;

	//FindColumns
	/////////////////////////////////////////////////
	int findFrom = (int)sQueryString.find("from");
	std::string col = sQueryString.substr(0, findFrom);
	CString columns(col.c_str());
	int lens = columns.GetLength();
	for (int i = 0; i < lens; i++) {
		if (columns[i] == ',')   whatCol++;
	}
	whatCol++;
	/////////////////////////////////////////////////

	do
	{
		for (int i = 0; i < whatCol; i++) {
			strTemp.Format(_T("%s%s$"), strTemp, _pDB->ValueString(i));
		}
		strTemp += "|";
		whatRow++;
	} while (_pDB->NextRow());


	CString CSwhatcol;
//	CSwhatcol.Format(_T("%d#"), whatCol);
	CSwhatcol.Format(_T("%d��"), whatCol);
	strTemp.Insert(0, CSwhatcol);

	CString CSwhatRow;
	CSwhatRow.Format(_T("%d,"), whatRow);
	strTemp.Insert(0, CSwhatRow);

	//////////////////////////////////////////////////////////////////////
	// Release DB process 
	//////////////////////////////////////////////////////////////////////
	// m_pDB->CloseConnection();
	//////////////////////////////////////////////////////////////////////

	return strTemp;
}

bool CDBPostgre::dbInsert(std::string QueryString)
{
	bool bRet = false;

	if (!_pDB) return false;

	bRet = _pDB->Insert(QueryString);

	// pSQL->CloseConnection();
	return bRet;
}

bool CDBPostgre::dbDelete(std::string QueryString)
{
	bool bRet = false;

	if (!_pDB) return false;

	bRet = _pDB->Delete(QueryString);

	// pSQL->CloseConnection();
	return bRet;
}

bool CDBPostgre::dbUpdate(std::string QueryString)
{
	bool bRet = false;

	if (!_pDB) return false;

	bRet = _pDB->Update(QueryString);

	return bRet;
}

//postgresql function call
bool CDBPostgre::dbFunctionExcute(std::string QueryString, int paramCount, const char* const* paramValues)
{
	bool bRet = false;

	if (!_pDB) return false;

	bRet = _pDB->FunctionExecute(QueryString, paramCount, paramValues);

	//�Լ� ȣ�� ��� ���ϰ� ó�� (row�� �׻� 0, col�� ���ϵǴ� �Ķ���� ������ ���� ����
	//The output from a function is always row=0 unless it returns a cursor.
	if (bRet == TRUE)	//bRet�� false�̾ �Լ� ������ ������ ����
	{
		CString strReturn;
		strReturn.Format(_T("%s"), _pDB->ValueString(0, 0));
		//���ϰ��� bool�� �̸� 't', 'f'�� ���� �Ѿ�ͼ� true, false�� �缳���ؼ� ǥ��
		if (strReturn.Compare(_T("t")) == 0) {
			strReturn = _T("true");
		} else if (strReturn.Compare(_T("f")) == 0) {
			strReturn = _T("false");
		}
	}

	return bRet;
}
