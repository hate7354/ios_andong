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
	CSwhatcol.Format(_T("%d▶"), whatCol);
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

	//함수 호출 결과 리턴값 처리 (row는 항상 0, col은 리턴되는 파라미터 갯수에 따라 조절
	//The output from a function is always row=0 unless it returns a cursor.
	if (bRet == TRUE)	//bRet가 false이어도 함수 실행은 성공한 것임
	{
		CString strReturn;
		strReturn.Format(_T("%s"), _pDB->ValueString(0, 0));
		//리턴값이 bool형 이면 't', 'f'로 값이 넘어와서 true, false로 재설정해서 표시
		if (strReturn.Compare(_T("t")) == 0) {
			strReturn = _T("true");
		} else if (strReturn.Compare(_T("f")) == 0) {
			strReturn = _T("false");
		}
	}

	return bRet;
}
