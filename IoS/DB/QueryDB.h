#pragma once

#include "pch.h"
#include <iostream>
#include <stdlib.h>
#include <libpq-fe.h>
#include <string>

#if _DBINI
extern std::string _DBname;
#define DEF_DB_SERVER_NAME   _DBname.c_str()//"iotr2"
#else
#define DEF_DB_SERVER_NAME   "iotr2"
#endif
#define DEF_DB_SERVER_USER   "postgres"

#if 1
#define DEF_DB_SERVER_PORT   "5648"
#define DEF_DB_SERVER_PASS   "@vcidbconnpw1419"
#else if
#define DEF_DB_SERVER_PORT   "5432"
#define DEF_DB_SERVER_PASS   "ubay1339"
#endif

class CQueryDB
{
public:
	CQueryDB(std::string &host);
	~CQueryDB();


public:
	PGconn* m_conn;
	PGresult* res;

	bool CreateQueryDB(std::string &host);

public:
	int field_num;
	int row_num;
	int res_index;

	BOOL AutoClose = TRUE;
public:
	PGconn *GetConnection();
	void CloseConnection();
	
	BOOL Insert(std::string sQuerty);
	BOOL Select(std::string sQuerty);
	BOOL Update(std::string querty);
	BOOL Delete(std::string querty);
	BOOL FunctionExecute(std::string querty, int paramCount, const char* const* paramValues);
	BOOL NextRow();

	int SetFirstRow();
	int SetLastRow();
	int ValueInt(int row, int col);

	CString  ValueString(int row, int col);
	CStringA  ValueStringA(int row, int col);
	int ValueInt(int col);
	CString  ValueString(int col);
	CStringA  ValueStringA(int col);
};

