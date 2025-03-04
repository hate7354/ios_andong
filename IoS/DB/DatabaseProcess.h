#pragma once
#include <cmath>
#include <vector>

#include "ChannelInfoDataSet.h"
#include "RoiInfoDataSet.h"
#include "RoiPointDataSet.h"
#include "AlgOptionInfoDataSet.h"
#include "AlgCamOptionInfoDataSet.h"
#include "ProcessInfoDataSet.h"

#include "define.h"
#include "DBPostgre.h"

class DatabaseProcess
{

public:
	DatabaseProcess(std::string& host);
	~DatabaseProcess();


public:
	CDBPostgre* _pPostDB = nullptr;
	bool CreateCDatabaseProcess(std::string &host);
	// CAlgorithmAttribute* m_pAttribut;
	// HMODULE hDataBaseModule = LoadLibrary(L"DBprocessingModule.dll");


//	connect_info (view table)
public:
	bool call_channel_info(int nidx, std::string sQueryString, std::string curation_ip);
	void SplitChannelInfoRecord(CString sRecord);
	void MakeChannelInfoRecord(CString sRecord);
//	roi_info 
	bool call_roi_info(int nidx, std::string sQueryString);
	void SplitRoiInfoRecord(CString sRecord);
	void MakeRoiInfoRecord(CString sRecord);
	uint32_t ParseAlgMask(CString sTok);
//	roi_point 
	bool call_roi_point_info(int nidx, std::string sQueryString);
	void SplitRoiPointInfoRecord(CString sRecord);
	void MakeRoiPointInfoRecord(CString sRecord);

	bool call_alg_option_info(int nidx, std::string sQueryString);
	void SplitAlgOptionInfoRecord(CString sRecord);
	void MakeAlgOptionInfoRecord(CString sRecord);

	bool call_alg_cam_option_info(int nidx, std::string sQueryString);
	void SplitAlgCamOptionInfoRecord(CString sRecord);
	void MakeAlgCamOptionInfoRecord(CString sRecord);

	bool call_process_info(int nidx, std::string sQueryString, std::string curation_ip);
	void SplitProcessInfoRecord(CString sRecord);
	void MakeProcessInfoRecord(CString sRecord);

public:
	std::vector<ChannelInfoDataSet*> _ChannelInfoDataBuff;
	std::vector<RoiInfoDataSet*> _RoiInfoDataBuff;
	std::vector<RoiPointDataSet*> _RoiPointInfoDataBuff;
	std::vector<AlgOptionInfoDataSet*> _AlgOptionInfoDataBuff;
	std::vector<AlgCamOptionInfoDataSet*> _AlgCamOptionInfoDataBuff;
	std::vector<ProcessInfoDataSet*> _ProcessInfoDataBuff;

public:
	
	ChannelInfoDataSet* _pChannelInfoRowData;
	RoiInfoDataSet* _pRoiInfoRowData;
	RoiPointDataSet* _pRoiPointInfoRowData;
	AlgOptionInfoDataSet* _pAlgOptionInfoRowData;
	AlgCamOptionInfoDataSet* _pAlgCamOptionInfoRowData;
	ProcessInfoDataSet* _pProcessInfoRowData;

	void SetChannelInfoDataBuff(ChannelInfoDataSet* pObject) { _ChannelInfoDataBuff.push_back(pObject); }
	void SetRoiInfoDataBuff(RoiInfoDataSet* pObject) { _RoiInfoDataBuff.push_back(pObject); }
	void SetRoiPointInfoDataBuff(RoiPointDataSet* pObject) { _RoiPointInfoDataBuff.push_back(pObject); }
	void SetAlgOptionInfoDataBuff(AlgOptionInfoDataSet* pObject) { _AlgOptionInfoDataBuff.push_back(pObject); }
	void SetAlgCamOptionInfoDataBuff(AlgCamOptionInfoDataSet* pObject) { _AlgCamOptionInfoDataBuff.push_back(pObject); }
	void SetProcessInfoDataBuff(ProcessInfoDataSet* pObject) { _ProcessInfoDataBuff.push_back(pObject); }

public:
	
	void ClearChannelInfoDataBuff() { _ChannelInfoDataBuff.clear(); }
	void ClearRoiInfoDataBuff() { _RoiInfoDataBuff.clear(); }
	void ClearRoiPointInfoDataBuff() { _RoiPointInfoDataBuff.clear(); }
	void ClearAlgOptionInfoDataBuff() { _AlgOptionInfoDataBuff.clear(); }
	void ClearAlgCamOptionInfoDataBuff() { _AlgCamOptionInfoDataBuff.clear(); }
	void ClearProcessInfoDataBuff() { _ProcessInfoDataBuff.clear(); }

	void ClearALLBuff() { 
		_ChannelInfoDataBuff.clear(); //20190515 connect_info 
		_RoiInfoDataBuff.clear(); //20190515 roi_info 
		_RoiPointInfoDataBuff.clear(); //20190515 roi_point
		_AlgOptionInfoDataBuff.clear();
		_AlgCamOptionInfoDataBuff.clear();
		_ProcessInfoDataBuff.clear();
	}

public:
	bool InsertAlgEvent(std::string sQueryString);
	bool InsertSYSEvent(std::string sQueryString);
	bool DeleteQuery(std::string sQueryString);
	bool InsertQuery(std::string sQueryString);
	bool UpdateQuery(std::string sQueryString);
	bool SelectQuery(std::string sQueryString);
};

