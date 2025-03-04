#pragma once

#include "QueryDB.h"

class CDBPostgre
{
public:
	CDBPostgre(std::string &host);
	~CDBPostgre();

public:
	CQueryDB* _pDB = nullptr;

public:
	bool DBprocessCreate(std::string &host);

	CString dbSelect(int nidx, std::string sQueryString);
	bool dbInsert(std::string QueryString);
	bool dbDelete(std::string QueryString);
	bool dbUpdate(std::string QueryString);
	bool dbFunctionExcute(std::string QueryString, int paramCount, const char* const* paramValues);
};

