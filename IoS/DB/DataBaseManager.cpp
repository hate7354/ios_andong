#include "pch.h"

#include <time.h>
#include "DataBaseManager.h"


DataBaseManager::DataBaseManager(std::string host, std::string curation_ip)
{
	_curation_ip = curation_ip;
	_pDatabaseProcess = new DatabaseProcess(host);
}


DataBaseManager::~DataBaseManager()
{
	if (_pDatabaseProcess) {
		_pDatabaseProcess->ClearALLBuff();
		delete _pDatabaseProcess;
		_pDatabaseProcess = nullptr;
	}
}

bool DataBaseManager::CreateDBprocess() 
{
	int nIdx = 1;
	bool bSQLRet = true;

	if (_pDatabaseProcess) {

		_pDatabaseProcess->ClearALLBuff();
		
		//20190515 start
		if (!ReSetDataforChannelInfoTB(_curation_ip)) return false;
		if (!ReSetDataforRoiInfoTB()) return false;
		if (!ReSetDataforRoiPointInfoTB()) return false;
		if (!ReSetDataforAlgOptionInfoTB()) return false;
		if (!ReSetDataforAlgCamOptionInfoTB()) return false;
		if (!ReSetDataforProcessInfoTB(_curation_ip)) return false;

		return true;
	}
	
	return false;
}


bool DataBaseManager::ReSetDataforChannelInfoTB(std::string curation_ip)
{
	return _pDatabaseProcess->call_channel_info(SQL_CHANNEL_INFO, DEF_SQL_CHANNEL_INFO, curation_ip);
}

bool DataBaseManager::ReSetDataforRoiInfoTB()
{
	return _pDatabaseProcess->call_roi_info(SQL_ROI_INFO, DEF_SQL_ROI_INFO);
}

bool DataBaseManager::ReSetDataforRoiPointInfoTB()
{
	return _pDatabaseProcess->call_roi_point_info(SQL_ROI_POINT_INFO, DEF_SQL_ROI_POINT_INFO);
}

bool DataBaseManager::ReSetDataforAlgOptionInfoTB()
{
	return _pDatabaseProcess->call_alg_option_info(SQL_ALG_OPTION_INFO, DEF_SQL_ALG_OPTION_INFO);
}

bool DataBaseManager::ReSetDataforAlgCamOptionInfoTB()
{
	return _pDatabaseProcess->call_alg_cam_option_info(SQL_ALG_CAM_OPTION_INFO, DEF_SQL_ALG_CAM_OPTION_INFO);
}

bool DataBaseManager::ReSetDataforProcessInfoTB(std::string curation_ip)
{
	return _pDatabaseProcess->call_process_info(SQL_PROCESS_INFO, DEF_SQL_PROCESS_INFO, curation_ip);
}

int DataBaseManager::DBDataSetCount(int nDBType) {

	int nRet = 99;
	switch (nDBType) {
	
	case SQL_CHANNEL_INFO:
		nRet = (int)_pDatabaseProcess->_ChannelInfoDataBuff.size();
		break;
	case SQL_ROI_INFO:
		nRet = (int)_pDatabaseProcess->_RoiInfoDataBuff.size();
		break;
	case SQL_ROI_POINT_INFO:
		nRet = (int)_pDatabaseProcess->_RoiPointInfoDataBuff.size();
		break;
	case SQL_ALG_OPTION_INFO:
		nRet = (int)_pDatabaseProcess->_AlgOptionInfoDataBuff.size();
		break;
	case SQL_ALG_CAM_OPTION_INFO:
		nRet = (int)_pDatabaseProcess->_AlgCamOptionInfoDataBuff.size();
		break;
	case SQL_PROCESS_INFO:
		nRet = (int)_pDatabaseProcess->_ProcessInfoDataBuff.size();
		break;
	default:
		nRet = 99;
		break;
	}

	return nRet;
}

