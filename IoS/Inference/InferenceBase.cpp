#include "pch.h"
#include "define.h"
#include "InferenceBase.h"

InferenceBase::InferenceBase(int nGPU, std::string cfg, std::string weights, int model)
{
	_cfg_file = cfg;
	_weights_file = weights;
	_gpuIdx = nGPU;
	_model = model;

	pDetector = new Detector(cfg, weights, nGPU);
}

InferenceBase::~InferenceBase()
{
	if (pDetector)
	{
		delete pDetector;
		pDetector = NULL;
	}
}


//bool InferenceBase::runInference(Mat& frame)
//{
//	if (frame.empty()) return false;
//
//	try {
//		std::vector<bbox_t> result_vect = pDetector->detect(frame,0.4);
//		for (int i = 0; i < result_vect.size(); i++) {
//			if (result_vect[i].x < 0 || result_vect[i].y < 0 || (result_vect[i].x + result_vect[i].w) > frame.cols || (result_vect[i].y + result_vect[i].h) > frame.rows)
//				continue;
//
//			if (_model == static_cast<int>(InferenceType::INF_OBJ)) {
//				if (result_vect[i].obj_id != 2)
//					continue;
//			}
//
//			return true;
//		}
//	}
//	catch (std::exception &e) { std::cerr << "exception: " << e.what() << "\n"; getchar(); }
//	catch (...) { std::cerr << "unknown exception \n"; getchar(); }
//
//	return false;
//}


bool InferenceBase::runInference(int ch_seq, Mat& frame)
{
    if (frame.empty()) return false;

    try {
        std::vector<bbox_t> result_vect = pDetector->detect(frame, 0.5);

        // 탐지된 객체가 없으면 즉시 리턴
        if (result_vect.empty()) {
            return false;
        }

        bool detected = false;
        int person_count = 0;  // 사람의 개수를 세기 위한 변수

        for (const auto& obj : result_vect) {
            // 객체가 이미지 범위를 벗어나면 무시
            if (obj.x < 0 || obj.y < 0 ||
                (obj.x + obj.w) > frame.cols ||
                (obj.y + obj.h) > frame.rows)
            {
                continue;
            }

            // 사람(obj_id == 0)인 경우만 처리
            if (obj.obj_id != 0) {
                continue;
            }

            // 사람 객체 개수 증가
            person_count++;

            // 좌표값 출력
            std::cout << "[Person Detected] ID: " << obj.obj_id
                << " | x: " << obj.x << " | y: " << obj.y
                << " | w: " << obj.w << " | h: " << obj.h << "\n";

            // ✅ ObjectCoord 객체 생성 후 push
            ObjectCoord coord(ch_seq, obj.x, obj.y, obj.w, obj.h);

            {   // 🔒 lock scope
                std::lock_guard<std::mutex> lock(g_CoordMtx);
                g_CoordQueue.push(coord);
            }

            g_CoordCV.notify_one();  // 데이터 추가됨을 알림
            detected = true;
        }

        // 탐지된 사람 개수가 0이면 false 반환
        return detected;
    }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in runInference: " << e.what() << "\n";
    }
    catch (...) {
        std::cerr << "[ERROR] Unknown exception occurred in runInference\n";
    }

    return false;
}




