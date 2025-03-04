#include "pch.h"
#include "DatabaseProcess.h"
#include <memory>

DatabaseProcess::DatabaseProcess(std::string &host)
{
	// m_pAttribut = new CAlgorithmAttribute();
	CreateCDatabaseProcess(host);
}


DatabaseProcess::~DatabaseProcess()
{
	ClearALLBuff();

	if (_pPostDB) {
		delete _pPostDB;
		_pPostDB = nullptr;
	}
}

bool DatabaseProcess::CreateCDatabaseProcess(std::string &host)
{
	_pPostDB = new CDBPostgre(host);

	if (!_pPostDB) return false;
	return true;
}

bool DatabaseProcess::UpdateQuery(std::string sQueryString)
{
	bool bRet = true;

	_pPostDB->dbUpdate(sQueryString);
	return bRet;
}

bool DatabaseProcess::DeleteQuery(std::string sQueryString)
{
	bool bRet = true;

	_pPostDB->dbDelete(sQueryString);
	return bRet;
}

bool DatabaseProcess::InsertQuery(std::string sQueryString)
{
	bool bRet = true;

	_pPostDB->dbInsert(sQueryString);
	return bRet;
}

template<typename ... Args>
std::string string_format(const std::string& format, Args ... args)
{
	size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
	std::unique_ptr<char[]> buf(new char[size]);
	snprintf(buf.get(), size, format.c_str(), args ...);
	return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}


///////////////////////////////////////////////////////////////////////////////////////////
// Connect_info Info Database sql - 필요에 따라 추가 20190515
///////////////////////////////////////////////////////////////////////////////////////////

// 	std::string QueryString1 = "select * from CONNECT_INFO(VIEW) ";
bool DatabaseProcess::call_channel_info(int nidx, std::string sQueryString, std::string curation_ip) {

	CString strTemp_part_det_info = _T("");

	std::string strTmp = string_format("%s %s '%s'", sQueryString.c_str(), "where CAM.use_yn = '1' and CAM.curation_server_id = CC.curator_seq and CC.curator_ip =", curation_ip.c_str());
//	std::cout << "strTmp = " << strTmp << std::endl;
	strTemp_part_det_info = _pPostDB->dbSelect(nidx, strTmp);  

	if (strTemp_part_det_info.GetLength() <= DEF_CHECK_DB_RESULT_CNT) return true;

	SplitChannelInfoRecord(strTemp_part_det_info);
	return true;
}

void DatabaseProcess::SplitChannelInfoRecord(CString sRecord) {

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;
	//AfxMessageBox(sRecord);
	
	// row counter
	AfxExtractSubString(sTok, sRecord, 0, ',');
	sCount = sTok;

	// records
//	AfxExtractSubString(sTok, sRecord, 1, '#');
	MakeExtractSubString(sTok, sRecord, 1, _T("▶"));
	sWholeRecord = sTok;

	int nMaxCount = _ttoi(sCount);
	int i;
	for (i = 0; i < nMaxCount; ++i) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		if (sSingleRecord.GetAt(0) == '$')
			break;

		//AfxMessageBox(sSingleRecord);
		//std::cout << "sSingleRecord : " << CT2CA(sSingleRecord) << std::endl;
		MakeChannelInfoRecord(sSingleRecord);
	}
}


void DatabaseProcess::MakeChannelInfoRecord(CString sRecord)
{
	CString sTok;
	ChannelInfoDataSet* pChannelInfoRowData = new ChannelInfoDataSet();

	int index = 0;

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setCamera_name(sTok);


	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setCamera_id(std::string(CT2CA(sTok)));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setCamera_seq(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setLogin_id(std::string(CT2CA(sTok)));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setLogin_pw(std::string(CT2CA(sTok)));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setRtsp_address(0, std::string(CT2CA(sTok)));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setRtsp_address(1, std::string(CT2CA(sTok)));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setSupport_ptz(std::string(CT2CA(sTok)));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setVideo_type(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pChannelInfoRowData->setErr_status(_ttoi(sTok));

	//AfxMessageBox(sTok);

	SetChannelInfoDataBuff(pChannelInfoRowData);
}
///////////////////////////////////////////////////////////////////////////////////////////

// 	std::string QueryString1 = "select * from ROI_INFO ";
bool DatabaseProcess::call_roi_info(int nidx, std::string sQueryString) {

	CString strTemp_part_det_info = _T("");
	
	strTemp_part_det_info = _pPostDB->dbSelect(nidx, sQueryString);
	//std::cout << strTemp_part_det_info << std::endl;
	if (strTemp_part_det_info.GetLength() <= DEF_CHECK_DB_RESULT_CNT) return false;

	SplitRoiInfoRecord(strTemp_part_det_info);
	return true;
}

void DatabaseProcess::SplitRoiInfoRecord(CString sRecord) {

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;
	//std::cout << "RoiInfo : " << sRecord << std::endl;
	// row counter
	AfxExtractSubString(sTok, sRecord, 0, ',');
	sCount = sTok;

	// records
//	AfxExtractSubString(sTok, sRecord, 1, '#');
	MakeExtractSubString(sTok, sRecord, 1, _T("▶"));
	sWholeRecord = sTok;

	int nMaxCount = _ttoi(sCount);
	int i;
	for (i = 0; i < nMaxCount; ++i) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		MakeRoiInfoRecord(sSingleRecord);
	}
}

void DatabaseProcess::MakeRoiInfoRecord(CString sRecord)
{
	CString sTok;
	RoiInfoDataSet* pRoiInfoRowData = new RoiInfoDataSet();

	int index = 0;
	AfxExtractSubString(sTok, sRecord, index++, '$');
	pRoiInfoRowData->setRoi_seq(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pRoiInfoRowData->setCamera_seq(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pRoiInfoRowData->setAlg_mask(ParseAlgMask(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pRoiInfoRowData->setCanvas_w(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pRoiInfoRowData->setCanvas_h(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pRoiInfoRowData->setRoi_idx(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pRoiInfoRowData->setAngle(_ttoi(sTok));

	SetRoiInfoDataBuff(pRoiInfoRowData);
}

uint32_t DatabaseProcess::ParseAlgMask(CString sTok)
{
	uint32_t mask = 0;

	CString sDiv;

	for (int i = 0; i < MAX_ALG_COUNT; i++) {
		if (::AfxExtractSubString(sDiv, sTok, i, '^')) {
			if (sDiv.CompareNoCase(L"alg01") == 0)
				mask |= (0x1 << 1);
			else if (sDiv.CompareNoCase(L"alg02") == 0)
				mask |= (0x1 << 2);
			else if (sDiv.CompareNoCase(L"alg03") == 0)
				mask |= (0x1 << 3);
			else if (sDiv.CompareNoCase(L"alg04") == 0)
				mask |= (0x1 << 4);
			else if (sDiv.CompareNoCase(L"alg05") == 0)
				mask |= (0x1 << 5);
			else if (sDiv.CompareNoCase(L"alg06") == 0)
				mask |= (0x1 << 6);
			else if (sDiv.CompareNoCase(L"alg07") == 0)
				mask |= (0x1 << 7);
			else if (sDiv.CompareNoCase(L"alg08") == 0)
				mask |= (0x1 << 8);
			else if (sDiv.CompareNoCase(L"alg09") == 0)
				mask |= (0x1 << 9);
			else if (sDiv.CompareNoCase(L"alg10") == 0)
				mask |= (0x1 << 10);
			else if (sDiv.CompareNoCase(L"alg11") == 0)
				mask |= (0x1 << 11);
			else if (sDiv.CompareNoCase(L"alg12") == 0)
				mask |= (0x1 << 12);
			else if (sDiv.CompareNoCase(L"alg13") == 0)
				mask |= (0x1 << 13);
			else if (sDiv.CompareNoCase(L"alg14") == 0)
				mask |= (0x1 << 14);
			else if (sDiv.CompareNoCase(L"alg15") == 0)
				mask |= (0x1 << 15);
			else if (sDiv.CompareNoCase(L"alg16") == 0)
				mask |= (0x1 << 16);
			else if (sDiv.CompareNoCase(L"alg20") == 0)
				mask |= (0x1 << 20);
		}
	}

	return mask;
}
///////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////
// Roi_point Info Database sql - 필요에 따라 추가 20190515
///////////////////////////////////////////////////////////////////////////////////////////

// 	std::string QueryString1 = "select * from ROI_POINT ";
bool DatabaseProcess::call_roi_point_info(int nidx, std::string sQueryString) {

	CString strTemp_part_det_info = _T("");
	strTemp_part_det_info = _pPostDB->dbSelect(nidx, sQueryString);

	if (strTemp_part_det_info.GetLength() <= DEF_CHECK_DB_RESULT_CNT) return false;

	SplitRoiPointInfoRecord(strTemp_part_det_info);
	return true;
}

void DatabaseProcess::SplitRoiPointInfoRecord(CString sRecord) {

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;
	//std::cout << "RoiPoint : " << sRecord << std::endl;
	// row counter
	AfxExtractSubString(sTok, sRecord, 0, ',');
	sCount = sTok;

	// records
//	AfxExtractSubString(sTok, sRecord, 1, '#');
	MakeExtractSubString(sTok, sRecord, 1, _T("▶"));
	sWholeRecord = sTok;

	int nMaxCount = _ttoi(sCount);
	int i;
	for (i = 0; i < nMaxCount; ++i) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		MakeRoiPointInfoRecord(sSingleRecord);
	}
}


void DatabaseProcess::MakeRoiPointInfoRecord(CString sRecord) {

	CString sTok;
	RoiPointDataSet* pRoiPointInfoRowData = new RoiPointDataSet();

	AfxExtractSubString(sTok, sRecord, 0, '$');
	pRoiPointInfoRowData->setRoi_seq(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 1, '$');
	pRoiPointInfoRowData->setPt_seq(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 2, '$');
	pRoiPointInfoRowData->setPt_x(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 3, '$');
	pRoiPointInfoRowData->setPt_y(_ttoi(sTok));

	SetRoiPointInfoDataBuff(pRoiPointInfoRowData);
}
///////////////////////////////////////////////////////////////////////////////////////////


bool DatabaseProcess::call_alg_option_info(int nidx, std::string sQueryString) {

	CString strTemp_part_det_info = _T("");
	strTemp_part_det_info = _pPostDB->dbSelect(nidx, sQueryString);

	if (strTemp_part_det_info.GetLength() <= DEF_CHECK_DB_RESULT_CNT) return false;

	SplitAlgOptionInfoRecord(strTemp_part_det_info);
	return true;
}

void DatabaseProcess::SplitAlgOptionInfoRecord(CString sRecord) {

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;
	//std::cout << "AlgOptionInfo : " << sRecord << std::endl;
	// row counter
	AfxExtractSubString(sTok, sRecord, 0, ',');
	sCount = sTok;

	// records
//	AfxExtractSubString(sTok, sRecord, 1, '#');
	MakeExtractSubString(sTok, sRecord, 1, _T("▶"));
	sWholeRecord = sTok;

	int nMaxCount = _ttoi(sCount);
	int i;
	for (i = 0; i < nMaxCount; ++i) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		MakeAlgOptionInfoRecord(sSingleRecord);
	}
}

void DatabaseProcess::MakeAlgOptionInfoRecord(CString sRecord) {

	CString sTok;
	AlgOptionInfoDataSet* pAlgOptionInfoRowData = new AlgOptionInfoDataSet();

	AfxExtractSubString(sTok, sRecord, 0, '$');
	pAlgOptionInfoRowData->setObject_min(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 1, '$');
	pAlgOptionInfoRowData->setObject_max(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 2, '$');
	pAlgOptionInfoRowData->setThreshold_min(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 3, '$');
	pAlgOptionInfoRowData->setThreshold_max(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 4, '$');
	pAlgOptionInfoRowData->setChg_Rate_max(_ttoi(sTok));

	SetAlgOptionInfoDataBuff(pAlgOptionInfoRowData);
}
///////////////////////////////////////////////////////////////////////////////////////////


bool DatabaseProcess::call_alg_cam_option_info(int nidx, std::string sQueryString) {

	CString strTemp_part_det_info = _T("");
	strTemp_part_det_info = _pPostDB->dbSelect(nidx, sQueryString);

	if (strTemp_part_det_info.GetLength() <= DEF_CHECK_DB_RESULT_CNT) return false;

	SplitAlgCamOptionInfoRecord(strTemp_part_det_info);
	return true;
}

void DatabaseProcess::SplitAlgCamOptionInfoRecord(CString sRecord) {

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;
	//std::cout << "AlgCamOptionInfo : " << sRecord << std::endl;
	// row counter
	AfxExtractSubString(sTok, sRecord, 0, ',');
	sCount = sTok;

	// records
//	AfxExtractSubString(sTok, sRecord, 1, '#');
	MakeExtractSubString(sTok, sRecord, 1, _T("▶"));
	sWholeRecord = sTok;

	int nMaxCount = _ttoi(sCount);
	int i;
	for (i = 0; i < nMaxCount; ++i) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		MakeAlgCamOptionInfoRecord(sSingleRecord);
	}
}

void DatabaseProcess::MakeAlgCamOptionInfoRecord(CString sRecord) {

	CString sTok;
	AlgCamOptionInfoDataSet* pAlgCamOptionInfoRowData = new AlgCamOptionInfoDataSet();

	int index = 0;
	AfxExtractSubString(sTok, sRecord, index++, '$');
	pAlgCamOptionInfoRowData->setCamera_seq(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pAlgCamOptionInfoRowData->setDaynight(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pAlgCamOptionInfoRowData->setObject_min(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pAlgCamOptionInfoRowData->setObject_max(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pAlgCamOptionInfoRowData->setThreshold_min(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pAlgCamOptionInfoRowData->setThreshold_max(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, index++, '$');
	pAlgCamOptionInfoRowData->setChg_Rate_max(_ttoi(sTok));

	SetAlgCamOptionInfoDataBuff(pAlgCamOptionInfoRowData);
}
///////////////////////////////////////////////////////////////////////////////////////////


bool DatabaseProcess::call_process_info(int nidx, std::string sQueryString, std::string curation_ip) {
	
	CString strTemp_part_det_info = _T("");

	if (curation_ip.length() > 0) {
		std::string strTmp = string_format("%s %s'%s'", sQueryString.c_str(), "where svr_ip=", curation_ip.c_str());
		strTemp_part_det_info = _pPostDB->dbSelect(nidx, strTmp);
	}
	else {
		strTemp_part_det_info = _pPostDB->dbSelect(nidx, sQueryString);
	}

	if (strTemp_part_det_info.GetLength() <= DEF_CHECK_DB_RESULT_CNT) return false;
	SplitProcessInfoRecord(strTemp_part_det_info);
	return true;
}

void DatabaseProcess::SplitProcessInfoRecord(CString sRecord) {

	CString sTok;
	CString sCount;
	CString sWholeRecord;
	CString sSingleRecord;
	//std::cout << "ProcessInfo : " << sRecord << std::endl;
	// row counter
	AfxExtractSubString(sTok, sRecord, 0, ',');
	sCount = sTok;

	// records
//	AfxExtractSubString(sTok, sRecord, 1, '#');
	MakeExtractSubString(sTok, sRecord, 1, _T("▶"));
	sWholeRecord = sTok;

	int nMaxCount = _ttoi(sCount);
	int i;
	for (i = 0; i < nMaxCount; ++i) {
		AfxExtractSubString(sSingleRecord, sWholeRecord, i, '|');

		MakeProcessInfoRecord(sSingleRecord);
	}
}


void DatabaseProcess::MakeProcessInfoRecord(CString sRecord) {

	CString sTok;
	ProcessInfoDataSet* pProcessInfoRowData = new ProcessInfoDataSet();

	AfxExtractSubString(sTok, sRecord, 0, '$');
	pProcessInfoRowData->setSend_curation(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 1, '$');
	pProcessInfoRowData->setOp_curation(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 2, '$');
	pProcessInfoRowData->setSend_alg(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 3, '$');
	pProcessInfoRowData->setOp_alg(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 4, '$');
	pProcessInfoRowData->setSend_config(_ttoi(sTok));

	AfxExtractSubString(sTok, sRecord, 5, '$');
	pProcessInfoRowData->setOp_config(_ttoi(sTok));

	SetProcessInfoDataBuff(pProcessInfoRowData);
}
///////////////////////////////////////////////////////////////////////////////////////////

