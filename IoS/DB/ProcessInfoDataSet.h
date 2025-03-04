#pragma once

#include <cmath>
#include <iostream>
#include <string>

class ProcessInfoDataSet
{
public:
	ProcessInfoDataSet() {};
	~ProcessInfoDataSet() {};

	//variable
private:
	int send_curation;
	int op_curation;
	int send_alg;
	int op_alg;
	int send_config;
	int op_config;

	//setter
public:
	void setSend_curation(int var) { send_curation = var; }
	void setOp_curation(int var) { op_curation = var; }
	void setSend_alg(int var) { send_alg = var; }
	void setOp_alg(int var) { op_alg = var; }
	void setSend_config(int var) { send_config = var; }
	void setOp_config(int var) { op_config = var; }

	//getter
public:
	int getSend_curation() { return send_curation; }
	int getOp_curation() { return op_curation; }
	int getSend_alg() { return send_alg; }
	int getOp_alg() { return op_alg; }
	int getSend_config() { return send_config; }
	int getOp_config() { return op_config; }
};
