#pragma once

#include "pch.h"
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>


#define	MAX_INF_MODEL_COUNT			2

#define	MAX_INF_GPU_COUNT			1

#define MAX_CHANNEL_COUNT			10
#define MAX_DNN_COUNT				(8 * MAX_INF_GPU_COUNT)
//#define MAX_CHANNEL_COUNT			1
//#define MAX_DNN_COUNT				(20 * MAX_INF_GPU_COUNT)
#define	MAX_CAMERA_COUNT			(MAX_CHANNEL_COUNT * MAX_DNN_COUNT)
#define	MAX_REAL_CAMERA_COUNT		((MAX_CAMERA_COUNT > 240) ? 240 : MAX_CAMERA_COUNT)

#define	MAX_ALG_COUNT				32
#define	MAX_ROI_COUNT				2

#define DEF_SQL_CHANNEL_INFO	"select camera_name, camera_id, cameras_seq, id, pw, rtsp_address, rtsp_address2, support_ptz, video_type, cam.err_stat from \"CAMERAS\" CAM, \"CURATOR_CONNECTION\" CC"
#define DEF_SQL_ROI_INFO		"select roi_seq, cameras_seq, alg_id1, canvas_w, canvas_h, roi_idx, angle from \"ROI_INFO\""
#define DEF_SQL_ROI_POINT_INFO	"select roi_seq, pt_seq, pt_x, pt_y from \"ROI_POINT\""
#define	DEF_SQL_ALG_OPTION_INFO	"select object_min, object_max, threshold_min, threshold_max, chg_rate_max from \"ALG_OPTION\""
#define	DEF_SQL_ALG_CAM_OPTION_INFO	"select cameras_seq, daynight, obj_min, obj_max, threshold_min, threshold_max, change_rate from \"CAMERA_SETTING\""
#define	DEF_SQL_PROCESS_INFO	"select send_curation, op_curation, send_alg, op_alg, send_config, op_config from \"CURATION_PROCESS\""

#define DEF_CHECK_DB_RESULT_CNT		3

#define	FILE_EVENT_PATH			L"D:\\Curation\\evt"
 
enum class InferenceType {
	INF_OBJ = 0,
	INF_FS = 1,
};

enum DB_SQL_TYPE {
	SQL_CONNECT_INFO = 20,
	SQL_ROI_INFO = 21,
	SQL_ROI_POINT_INFO = 22,
	SQL_ALG_OPTION_INFO = 23,
	SQL_ALG_CAM_OPTION_INFO = 24,
	SQL_PROCESS_INFO = 25,
	SQL_CONNECT_THERMAL_INFO = 26,
	SQL_CHANNEL_INFO = 27,
	SQL_EVENT = 99
};

typedef struct RoiInfo {
	uint32_t algMask;
	std::vector<CPoint> roiPts;
	int angle;
} RoiInfo_t;

typedef struct AlgOpt {
	int32_t objMin;
	int32_t objMax;
	int32_t thresMin;
	int32_t thresMax;
	int32_t chgRateMax;
} AlgOpt_t;

typedef struct ChInfo {
	int group_index;		//dnn index 저장
	int ch_seq;
	std::string ch_id;
	CString	strChName;		//DB에는 camera_name
	std::string	ch_ip;		//안씀!
	bool is_ptz;
	std::string rtsp_address[2];
	std::string login_id;
	std::string login_pass;
	int alg_mode;		//global별이냐, channel별이냐
	RoiInfo_t roi_Info[MAX_ROI_COUNT];
	AlgOpt_t cam_opt[2];	//채널별일때 민감도, obj크기, 변화율 정보 - index==0 낮, index==1 밤
	std::vector<int> durTimes;		//채널별일때 duration 설정 값
	int status;				//고장 상태 0-정상, 1-고장
} ChInfo_t;

struct CmdHeaderII {
	uint8_t		cmd;			//명령어 타입
	uint8_t		category;		//안씀 어떤 타입 명령어인지 확인하려고 했음
	uint8_t		reply;			//안씀 응답하는쪽에서 사용하려고 했음
	uint8_t		data;
};

//글로벌 공용 큐 / 동기화 객체

// ObjectCoord 구조체 정의 예시
struct ObjectCoord {
	int ch_seq;
	int x;
	int y;
	int w;
	int h;

	// ✅ 생성자 추가
	ObjectCoord(int ch, int x_, int y_, int w_, int h_)
		: ch_seq(ch), x(x_), y(y_), w(w_), h(h_) {}
};


// ✅ 전역 변수 선언 (extern)
extern std::mutex g_CoordMtx;
extern std::queue<ObjectCoord> g_CoordQueue;
extern std::condition_variable g_CoordCV;
