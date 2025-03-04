#pragma once

#include "DatabaseProcess.h"

class DataBaseManager
{
public:
	DataBaseManager(std::string host, std::string curation_ip);
	~DataBaseManager();

public:
	DatabaseProcess* _pDatabaseProcess = nullptr;

public:
	bool CreateDBprocess();



	bool ReSetDataforChannelInfoTB(std::string curation_ip);
	bool ReSetDataforRoiInfoTB();
	bool ReSetDataforRoiPointInfoTB();
	bool ReSetDataforAlgOptionInfoTB();
	bool ReSetDataforAlgCamOptionInfoTB();
	bool ReSetDataforProcessInfoTB(std::string curation_ip);


public:
	int DBDataSetCount(int nDBType);

private:
	std::string _curation_ip;
};

