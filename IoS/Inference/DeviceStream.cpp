#include "pch.h"
#include "DBPostgre.h"
#include "DeviceStream.h"
#include "../json/json.h"

#include <winsock2.h>

extern bool g_bAlgorithm;

extern std::vector<uint8_t> g_AICons;
extern std::vector<uint8_t>	g_AIRecvCons;
extern std::mutex g_AIMtx;
extern std::vector<std::string> g_AIIPs;
extern std::vector<CString> g_AINames;

extern std::mutex	g_MtxConn;
extern int g_nConnect;

extern bool						g_bMaster;
extern int						g_nMasterInx;
extern std::vector<std::string>	g_IoSLists;
extern std::map<std::pair<std::string, int>, uint8_t> g_IoSCons;
extern std::mutex				g_IoSMtx;



extern CString g_strIoSName;


DeviceStream::DeviceStream(std::vector<InferenceStream*> pInfStreams, GroupChannelManager* mgr, std::string evtIP, std::string curIP, EventManager* evtMgr)
	: _pInfStreams(pInfStreams)
	, _pGrpMgr(mgr)
	, _evtIP(evtIP)
	, _curIP(curIP)
	, _pEvtMgr(evtMgr)
{
	av_register_all();
	avcodec_register_all();

	CreateCodec(0);

	_evt_start = std::chrono::steady_clock::now();
	_frame_tp = _evt_start;
	_trans_tp = _evt_start;

	_bLoop = true;
	_th = std::thread{ &DeviceStream::StreamThread, this };		//영상 decoding -> decoding한 fream Q에 push
	_thSnd = std::thread{ &DeviceStream::TCPSendThread, this };	//Tcp로 메세지 send 할 때 사용하는 Thread
}

DeviceStream::~DeviceStream()
{
	StopThreads();
	DisconnectAll();

	if (_th.joinable())
		_th.join();

	if (_thSnd.joinable())
		_thSnd.join();

	_streamDataQ.clear();


	if (_pVolData) {
		delete[] _pVolData;
		_pVolData = nullptr;
	}

	_volSize = 0;

	while (_listTcp.size() > 0) {
		uint8_t* pData = _listTcp.front();
		if (pData)
			delete[] pData;
		_listTcp.pop_front();
	}

	while (_listSnd.size() > 0) {
		uint8_t* pData = _listSnd.front();
		if (pData)
			delete[] pData;
		_listSnd.pop_front();
	}

	for (int i = 0; i < _pInfStreams.size(); i++) {
		if (_pInfStreams[i]) {
			_pInfStreams[i]->removeCameraSeq(_ch_seq);
			_pInfStreams[i]->removeEventCamera(_ch_seq);
		}
	}

	DeleteCodec();
}

void DeviceStream::DisconnectAll()
{
	Disconnect();
	DisconnectAI();
}

void DeviceStream::StopThreads()
{
	_bLoop = false;
	_condSnd.notify_one();
}

bool a = true;
int cnt2 = 0;
std::chrono::time_point<std::chrono::steady_clock> _start_time2;
void DeviceStream::setVideoStream(VideoStreamData* pStream)
{
	if (_bLoop) {
		if (pStream->frameType == 3) {
			pStream->frameType = 1;
			_streamDataQ.push(pStream, _pVolData, _volSize);
			cnt2++;
		}
		else {
			_streamDataQ.push(pStream);
			cnt2++;
		}

		if (a) {
			_start_time2 = std::chrono::steady_clock::now();
			a = false;
		}
		std::chrono::time_point<std::chrono::steady_clock> tp2 = std::chrono::steady_clock::now();
		std::chrono::duration<double> spent2 = tp2 - _start_time2;
		if (spent2.count() >= 1) {
			_start_time2 = std::chrono::steady_clock::now();
			//cout << "_ch_seq : " << _ch_seq << ", frame count : " << cnt2 << endl;
			cnt2 = 0;
		}

		
	}
}
//FILE* fp_dump = NULL;
//int dump_num = 0;
void DeviceStream::StreamThread()		//영상 받아서 decoding
{
//	string dump_name = "ios_h264_" + to_string(dump_num) + ".h264";
//	dump_num++;
//	cout << "camera_name : " << dump_num << endl;
//	char dump_name2[20];
//	strcpy(dump_name2, dump_name.c_str());
//	fp_dump = fopen(dump_name2, "wb");
	int remainCount = 0;
	std::chrono::time_point<std::chrono::steady_clock> rec_tp = std::chrono::steady_clock::now();

	while (_bLoop) {
		if (_bChangeInfo == true) {
			_streamDataQ.clear();

			if (_pVolData) {
				delete _pVolData;
				_pVolData = nullptr;
			}
			_volSize = 0;

			if (_ch_seq > 0) {
				for (int i = 0; i < _pInfStreams.size(); i++) {
					if (_pInfStreams[i]) {
						_pInfStreams[i]->removeCameraSeq(_ch_seq);
						_pInfStreams[i]->removeEventCamera(_ch_seq);
					}
				}
			}

			_CamInfo = _CamInfo_copy;

			_strChannelName = _CamInfo.strChName;
			_ch_seq = _CamInfo.ch_seq;
			_ch_id = _CamInfo.ch_id;
			_group_Index = _CamInfo.group_index;
			
			for (int i = 0; i < _pInfStreams.size(); i++) {
				if (_pInfStreams[i])
					_pInfStreams[i]->addCameraSeq(_ch_seq);
			}

			_bFirstFrame = true;
			_bChangeInfo = false;
		}

		if (_conn_state == 2) {	//kimdh - 0618 rtsp connect
			if (_CamInfo.status == 0) {
				//cout << "connect : " << _ch_seq << endl;
				Connect();
			}
		}

		bool bRecEvtImg = false;
		
#if 1
		while (VideoStreamData* pStream = _streamDataQ.front()) {
			if (pStream->streamDataType != _preCodec) {
				DeleteCodec();

				_preWidth = -1;
				_preHeight = -1;
			}

			if (_bReadyCodec == false)
				CreateCodec(pStream->streamDataType);

			bool bSkip = true;
			if (_bEvent == false) {
				bool bOccur = false;
				for (int i = 0; i < _pInfStreams.size(); i++) {
					if (_pInfStreams[i]) {
						if (_pInfStreams[i]->checkEventCamera(_ch_seq, true)) {
							bOccur = true;
							break;
						}
					}
				}

				if (bOccur) {
					std::chrono::time_point<std::chrono::steady_clock> tp = std::chrono::steady_clock::now();
					std::chrono::duration<double> spent = tp - _evt_start;
					if (spent.count() >= 1.0) {
						_evt_start = tp;
					}
					else {
						for (int i = 0; i < _pInfStreams.size(); i++) {
							if (_pInfStreams[i])
								_pInfStreams[i]->removeEventCamera(_ch_seq);
						}
					}
				}

				bOccur = false;
				for (int i = 0; i < _pInfStreams.size(); i++) {
					if (_pInfStreams[i]) {
						if (_pInfStreams[i]->checkEventCamera(_ch_seq, true)) {
							bOccur = true;
							break;
						}
					}
				}
#if NoDnn
				bOccur = true;
#endif
				if (bOccur) {
					//cout << "Event1 = " << to_string(_ch_seq) << endl;
					int idx = -1;
					bool connected = ConnectToAI(idx);

					if (connected) {
						//cout << "Event2 = " << to_string(_ch_seq) << endl;
						_bEvent = true;
						//cout << "Event3 = " << _listTcp.size() << endl;
						while (_listTcp.size() > 0) {
							uint8_t *pData = _listTcp.front();
							if (pData) {
								VideoHeader* pHeader = (VideoHeader*)pData;
								if (pHeader->frame_type == 1) {
									std::chrono::time_point<std::chrono::steady_clock> tp = std::chrono::steady_clock::now();
									std::chrono::duration<double> spent = tp - _frame_tp;
									if (spent.count() > 0.9) {
										remainCount = _TransCount;		//std::min(_TransCount, _pGrpMgr->GetFrame(_group_Index));
										//remainCount = 30;
										_frame_tp = tp;
									}
								}

#if NoDnn
								if (true) {	//remainCount > 0) {	//full frame
#else if
								if (remainCount > 0) {
#endif
									bool bDetected = false;
									if (_listTcp.size() == 1) {
										bDetected = true;
										remainCount--;
									}

									pHeader->bDetected = bDetected;
									
									PushSendData(pData);
								}
								else {
									delete[] pData;
								}
							}
							_listTcp.pop_front();
						}
					}
					else {
						bRecEvtImg = true;

#if 0		//kimdh0721 - R2 고장처리
						if (idx >= 0) {
//							cout << "Broken R2" << endl;
							UpdateAIState(idx, false);
						}
#endif
						
						while (_listTcp.size() > 0) {
							uint8_t* pData = _listTcp.front();
							if (pData)
								delete[] pData;
							_listTcp.pop_front();
						}
						

						for (int i = 0; i < _pInfStreams.size(); i++) {
							if (_pInfStreams[i])
								_pInfStreams[i]->removeEventCamera(_ch_seq);
						}
					}
					_streamInCnt = 0;
				}
			}

			if (_bEvent) {
				if (!g_bAlgorithm) {								// Algorithm Stop 하면 Event H264 bypass 동작도 멈춘다
					for (int i = 0; i < _pInfStreams.size(); i++) {
						if (_pInfStreams[i])
							_pInfStreams[i]->removeEventCamera(_ch_seq);
					}
//					cout << "Discon 1 = " << to_string(_ch_seq) << endl;
					DisconnectAI();
					_bEvent = false;
				}
				else {
					if (pStream->frameType == 1) {
						std::chrono::time_point<std::chrono::steady_clock> tp = std::chrono::steady_clock::now();
						std::chrono::duration<double> spent = tp - _frame_tp;
						if (spent.count() > 0.9) {
							remainCount = _TransCount;		//std::min(_TransCount, _pGrpMgr->GetFrame(_group_Index));
							//remainCount = 30;
							_frame_tp = tp;
						}
					}

#if NoDnn
					if (true) {	//remainCount > 0) {	//full frame
#else if
					if (remainCount > 0) {
#endif
						uint8_t* pData = MakeSendData(pStream, true);
						if (pData)
							PushSendData(pData);
						remainCount--;
					}
				}
			}
			else {
				if (pStream->frameType == 1) {		//frameType == 1 -> i frame(==key frame)

					while (_listTcp.size() > 0) {
						uint8_t* pData = _listTcp.front();
						if (pData)
							delete[] pData;
						_listTcp.pop_front();
					}


					std::chrono::time_point<std::chrono::steady_clock> tp = std::chrono::steady_clock::now();
					if (_bFirstFrame) {
						_streamInCnt = _pGrpMgr->GetFrame(_group_Index);		//keyframe count 갱신(keyframe 갱신)
						_trans_tp = tp;
						_bFirstFrame = false;
					}
					else {
						std::chrono::duration<double> spent = tp - _trans_tp;
#if NoDnn
						bSkip = false;	//full frame
#endif
						if (spent.count() > 0.9) {
							_streamInCnt = _pGrpMgr->GetFrame(_group_Index);
							_trans_tp = tp;
						}
					}
				}

#if NoDnn
				bSkip = false;	//full frame
#endif
				if (_streamInCnt > 0) {
					_streamInCnt--;

					bSkip = false;

					if (nalunit_first == 0) {
						uint8_t* pData = MakeSendData(pStream, false);
						if (pData)
							_listTcp.push_back(pData);
					}
				}
				else {
					nalunit_first = 1;
				}
			}

			_ns_count = 0;
			if (_signal_state == false) {
				_signal_state = true;
				UpdateErrorMgr();
			}

			if (bSkip) {
				AVPacket packet = { 0, };
//				uint8_t* data = nullptr;

				packet.size = pStream->streamDataSize;
				packet.data = new uint8_t[packet.size];
				::memcpy(packet.data, pStream->streamData, packet.size);

				AVFrame vframe = { 0, }; 

				int success = 0;
				avcodec_decode_video2(_pCodecCtx, &vframe, &success, &packet);
				if (success) {
					if (_preWidth < 0 || _preHeight < 0) {
						_preWidth = vframe.width;
						_preHeight = vframe.height;
					}
					else if (_preWidth != vframe.width || _preHeight != vframe.height) {
						if (packet.data) {
							delete[] packet.data;
						}

						av_packet_unref(&packet);
						av_frame_unref(&vframe);

						DeleteCodec();

						_preWidth = -1;
						_preHeight = -1;

						_bReadyCodec = false;

						continue;
					}

					if (g_bAlgorithm) {
						AVFrame dst;
						struct SwsContext* convert_ctx = nullptr;

						int outWidth = vframe.width > 640 ? 640 : vframe.width;
						int outHeight = vframe.height > 480 ? 480 : vframe.height;

						Mat frame = Mat(outHeight, outWidth, CV_8UC3);
						dst.data[0] = (uint8_t*)frame.data;
						avpicture_fill((AVPicture*)&dst, dst.data[0], AVPixelFormat::AV_PIX_FMT_BGR24, outWidth, outHeight);

						AVPixelFormat src_pixfmt = (AVPixelFormat)vframe.format;
						AVPixelFormat dst_pixfmt = AVPixelFormat::AV_PIX_FMT_BGR24;
						convert_ctx = sws_getContext(vframe.width, vframe.height, src_pixfmt, outWidth, outHeight, dst_pixfmt, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
						sws_scale(convert_ctx, vframe.data, vframe.linesize, 0, vframe.height, dst.data, dst.linesize);
						sws_freeContext(convert_ctx);

						if (frame.cols != 640 || frame.rows != 480)
							resize(frame, frame, cv::Size(640, 480), cv::INTER_LINEAR);

						if (bRecEvtImg) {
							std::chrono::time_point<std::chrono::steady_clock> tp = std::chrono::steady_clock::now();
							std::chrono::duration<double> spent = tp - rec_tp;
							if (spent.count() >= 5.0) {
								rec_tp = tp;
								//SendEvent(_ch_id, _ch_seq, pStream->streamTime, _curIP, frame);
							}
							bRecEvtImg = false;
						}
						else {
							pushFrame(frame);
						}
					}
					retry_voldata = false;
				}
				else {
					cout << "dec fail = " << to_string(_ch_seq) << endl;
					retry_voldata = true;
				}

				if (packet.data) {
					delete[] packet.data;
				}
				av_packet_unref(&packet);
				av_frame_unref(&vframe);
			}

			_streamDataQ.pop();

			if (_conn_state == 2) {
				_streamDataQ.clear();
				break;
			}

			if (_bChangeInfo == true)
				break;
//			if (fp_dump) fwrite(pStream->streamData, pStream->streamDataSize, 1, fp_dump);
		}
#endif

		if (_conn_state == 1) {
			_ns_count++;
			if (_ns_count >= 60) {		//50
				std::unique_lock<std::mutex>lock(g_MtxConn);
				g_nConnect--;
				//cout << "conn_state : " << _ch_seq << endl;
				_conn_state = 2;

#if 0		//kimdh0721 - camera 고장처리
				if (_CamInfo.status == 0) {
					if (++_retry_connect > 3) {
						_CamInfo.status = 1;
						UpdateCameraState(1);
					}
				}
#endif
				if (_CamInfo.status == 0) {
//					_CamInfo.status = 1;
					UpdateCameraState(1);
					_signal_state = false;
				}
			}
		}
	}
//	if (fp_dump) fclose(fp_dump);
}

void DeviceStream::TCPSendThread()
{
	TTCPPacketHeader header;
	::memcpy(&header.code, TCPSessionParser::PACKET_MAGIC_CODE, 4);

	while (_bLoop) {
		std::unique_lock<std::mutex> lock(_mtxSnd);
		if (_listSnd.empty())
			_condSnd.wait(lock);
		
		if (_listSnd.size() > 0) {
			uint8_t* pData = _listSnd.front();
			_listSnd.pop_front();
			lock.unlock();

			if (pData) {
				if (_bEvent == true && _bPTZLock == false) {
					if (_bConnected == true) {
						VideoHeader* pHeader = (VideoHeader*)pData;
						if (_sndSock != INVALID_SOCKET) {
							int len = sizeof(VideoHeader) + pHeader->buf_size;		//kimdh r2send
							header.contentLength = len;
							//cout << "header : " << sizeof(header) << ", len : " << len << endl;
							//cout << "camera_seq : " << pHeader->camera_seq << ", codec_type : " << pHeader->codec_type << ", frame_type : " << pHeader->frame_type << ", bDetected : " << pHeader->bDetected << ", buf_size : " << pHeader->buf_size << endl;
							if (::send(_sndSock, (const char*)&header, sizeof(header), 0) != sizeof(header)) {
								_evt_start = std::chrono::steady_clock::now();
								DisconnectAI();
								_bEvent = false;
							}
							else {
								if (::send(_sndSock, (const char*)pData, len, 0) != len) {
									_evt_start = std::chrono::steady_clock::now();
									DisconnectAI();
									_bEvent = false;
								}
							}
						}
					}
				}
				delete[] pData;
			}
		}
	}
}

void DeviceStream::pushFrame(Mat &frame)
{
	if (_bPTZLock == false) {
		for (int i = 0; i < _pInfStreams.size(); i++) {
			if (_pInfStreams[i])
				_pInfStreams[i]->pushFrame(_ch_seq, frame);
		}
	}
	else cout << _ch_seq << "번 카메라 PTZ LOCK " << endl;
}

bool DeviceStream::Connect(std::string host, uint16_t rtsp_port, std::string uri, std::string id, std::string pass)
{
	_hostName = host;
	_rtsp_port = rtsp_port;
	_userID = id;
	_password = pass;
	_uri = uri;

	_ns_count = 0;
	_conn_state = 2;

	return true;
}

bool DeviceStream::Connect()
{
	_ns_count = 0;
	_conn_state = 0;

	if (_pRtspClient) {
		_pRtspClient->closeURL();
		delete _pRtspClient;
		_pRtspClient = nullptr;
	}
	
	std::string rtsp_url;
	
	if (_userID.empty() || _password.empty())
		rtsp_url = "rtsp://" + _hostName + ":" + std::to_string(_rtsp_port) + "/" + _uri;
		//rtsp_url = "rtsp://192.168.50.156:10554/proxied1";
	else
		rtsp_url = "rtsp://" + _userID + ":" + _password + "@" + _hostName + ":" + std::to_string(_rtsp_port) + "/" + _uri;
//	cout << "url = " << rtsp_url << endl;

	_pRtspClient = new RTSPClient();
	_pRtspClient->fHandler = this;
	//cout << "rtsp : " << rtsp_url << endl;
	if (_pRtspClient->openURL(rtsp_url.c_str(), 1, 2) != 0) {		//TCP
	//if (_pRtspClient->openURL(rtsp_url.c_str(), 0, 2) != 0) {		//UDP
		delete _pRtspClient;
		_pRtspClient = nullptr;
		_conn_state = 2;
		
		if (_retry_connect == 0) {
			_retry_connect = 1;
			UpdateCameraState(2);
		}
		else if(++_retry_connect > 1000) {		//4
//			_CamInfo.status = 1;
			_retry_connect = 1;
			UpdateCameraState(2);
		}
		 
		return false;
	}

	_volSize = _pRtspClient->videoExtraDataSize();
	if (_volSize > 0) {
		_pVolData = new uint8_t[_volSize];
		::memcpy(_pVolData, _pRtspClient->videoExtraData(), _volSize);
	}

	if (_pRtspClient->playURL(frameHandlerFunc, _pRtspClient, nullptr, nullptr) != 0) {
		_pRtspClient->closeURL();
		delete _pRtspClient;
		_pRtspClient = nullptr;
		_conn_state = 2;

		return false;
	}

//	cout << "connect = " << to_string(_ch_seq) << endl;
	std::unique_lock<std::mutex>lock(g_MtxConn);
	g_nConnect++;

	_conn_state = 1;
	_retry_connect = 1;

	_bIdleState = false;

	if (_signal_state == true) {
		UpdateErrorMgr();
	}

	return true;
}

void DeviceStream::Disconnect()
{
	if (_pRtspClient) {
		_pRtspClient->closeURL();
		delete _pRtspClient;
		_pRtspClient = nullptr;
	}

	std::unique_lock<std::mutex>lock(g_MtxConn);
	if (_conn_state == 1)
		g_nConnect--;

//	cout << "disconnect = " << to_string(_ch_seq) << endl;
	_conn_state = 0;
	_bIdleState = true;
}

void DeviceStream::frameHandlerFunc(void* arg, RTP_FRAME_TYPE frame_type, int64_t timestamp, unsigned char* buf, int length)
{
	RTSPClient* pRtspClient = (RTSPClient*)arg;
	if (pRtspClient == nullptr)
		return;

	DeviceStream* pDeviceStream = (DeviceStream*)pRtspClient->fHandler;
	if (pDeviceStream == nullptr)
		return;

	if (frame_type != RTP_FRAME_TYPE::FRAME_TYPE_VIDEO)
		return;

	VideoStreamData videoStream;
	::memset(&videoStream, 0x00, sizeof(VideoStreamData));

	std::string codec = pRtspClient->videoCodec();

	if (codec == "H264") {
		videoStream.streamDataType = 0;
		uint8_t frag = buf[4] & 0x1F;

		if (frag == 1 || frag == 5 || frag == 7 || frag == 8) {
			if (frag == 7) {
				if (length < 2048) {
					int length2 = 0;
					if (pDeviceStream->_volSize > 0) {
						if (memcmp(pDeviceStream->_pVolData, buf, pDeviceStream->_volSize)) {
							int step1 = 0, step2 = 0;
							for (int i = 4; i < length; i++) {
								if (buf[i - 4] == 0 && buf[i - 3] == 0 && buf[i - 2] == 0 && buf[i - 1] == 1) {
									if (step1 == 1 && step2 == 1) {
										length2 = i - 4;
										break;
									}

									uint8_t frag2 = buf[i] & 0x1F;
									if (frag2 == 7) {
										step2 = 0;
										step1 = 1;
									}
									else if (frag2 == 8) {
										if (step1 == 0)
											continue;

										step2 = 1;
									}
								}
							}

							if (length2 == 0)
								length2 = length;
						}
					}
					else {
						int step1 = 0, step2 = 0;
						for (int i = 4; i < length; i++) {
							if (buf[i - 4] == 0 && buf[i - 3] == 0 && buf[i - 2] == 0 && buf[i - 1] == 1) {
								if (step1 == 1 && step2 == 1) {
									length2 = i - 4;
									break;
								}

								uint8_t frag2 = buf[i] & 0x1F;
								if (frag2 == 7) {
									step2 = 0;
									step1 = 1;
								}
								else if (frag2 == 8) {
									if (step1 == 0)
										continue;

									step2 = 1;
								}
							}
						}

						if (length2 == 0)
							length2 = length;
					}

					if (pDeviceStream->retry_voldata) {
						if (length2 > 0) {
							if (pDeviceStream->_pVolData) {
								delete[] pDeviceStream->_pVolData;
								pDeviceStream->_pVolData = nullptr;
							}

							pDeviceStream->_pVolData = new uint8_t[length2];
							::memcpy(pDeviceStream->_pVolData, buf, length2);
							pDeviceStream->_volSize = length2;
						}
					}
					
					return;
				}

				videoStream.frameType = 1;
			}
			else if (frag == 5) {
				videoStream.frameType = 3;
			}
		}
		else {
			return;
		}
	}
	else if (codec == "JPEG") {
		videoStream.streamDataType = 1;
		videoStream.frameType = 1;
	}
	else if (codec == "H265") {
		videoStream.streamDataType = 2;
		uint8_t frag = (buf[4] & 0x7E) >> 1;
		if (frag == 32 || frag == 33 || frag == 34 || frag == 19)
			videoStream.frameType = 1;
	}
	else {
		return;
	}

	videoStream.streamData = buf;
	videoStream.streamDataSize = length;

	GetLocalTime(&videoStream.streamTime);
	pDeviceStream->setVideoStream(&videoStream);
}

bool DeviceStream::CreateCodec(int codecType)
{
	if (codecType == 0)
		_pCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
	else if (codecType == 1)
		_pCodec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
	else if (codecType == 2)
		_pCodec = avcodec_find_decoder(AV_CODEC_ID_H265);
	else
		return false;


	if (_pCodec) {
		_pCodecCtx = avcodec_alloc_context3(_pCodec);
		if (_pCodecCtx) {
			if (avcodec_open2(_pCodecCtx, _pCodec, nullptr) < 0)
				return false;

			_preCodec = codecType;
			_bReadyCodec = true;
			return true;
		}
	}

	return false;
}

void DeviceStream::DeleteCodec()
{
	if (_pCodecCtx) {
		avcodec_close(_pCodecCtx);
		avcodec_free_context(&_pCodecCtx);
		_pCodecCtx = nullptr;
	}

	_preCodec = -1;
	_bReadyCodec = false;
}

bool DeviceStream::ConnectToAI(int& aidx)
{
	bool retval = false;
	uint8_t cnt;
	int index;

	bool bMaster = false;
	int nMasterInx = -1;
	std::unique_lock<std::mutex> lock(g_IoSMtx);
	bMaster = g_bMaster;
	nMasterInx = g_nMasterInx;
	lock.unlock();

	if (bMaster == false) {
		sockaddr_in hint;
		hint.sin_family = AF_INET;
		hint.sin_port = htons(9000);
		int ret = inet_pton(AF_INET, g_IoSLists[nMasterInx].c_str(), &hint.sin_addr);
		if (ret != 1)
			return retval;

		SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET)
			return retval;

		ret = ::connect(sock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint));
		if (ret == SOCKET_ERROR) {
			closesocket(sock);
			sock = INVALID_SOCKET;
			return retval;
		}

		TTCPPacketHeader header;
		::memcpy(&header.code, TCPSessionParser::PACKET_MAGIC_CODE, 4);
		header.contentLength = sizeof(CmdHeaderII);

		CmdHeaderII data;
		::memset(&data, 0x00, sizeof(data));

		if (::send(sock, (const char*)&header, sizeof(header), 0) != sizeof(header)) {
			closesocket(sock);
			sock = INVALID_SOCKET;
			return retval;
		}

		if (::send(sock, (const char*)&data, sizeof(data), 0) != sizeof(data)) {
			closesocket(sock);
			sock = INVALID_SOCKET;
			return retval;
		}

		TTCPPacketHeader headerR;
		ret = recv(sock, (char*)&headerR, sizeof(TTCPPacketHeader), 0);
		if (ret != sizeof(TTCPPacketHeader)) {
			closesocket(sock);
			sock = INVALID_SOCKET;
			return retval;
		}

		if (headerR.code[0] != 'T' || headerR.code[1] != 'C' || headerR.code[2] != 'R' || headerR.code[3] != 'S') {
			closesocket(sock);
			sock = INVALID_SOCKET;
			return retval;
		}
		
		CmdHeaderII ReplyHeader;
		ret = recv(sock, (char*)&ReplyHeader, sizeof(CmdHeaderII), 0);
		if (ret != sizeof(CmdHeaderII)) {
			closesocket(sock);
			sock = INVALID_SOCKET;
			return retval;
		}

		closesocket(sock);
		sock = INVALID_SOCKET;

		index = -1;
		if (ReplyHeader.reply == 0)
			index = ReplyHeader.data - 1;

		bool matched = false;
		if (index >= 0 && index < g_AICons.size() - 1) {
			matched = true;
		}
		else if (index < 0) {
			g_AIMtx.lock();
			index = g_AICons.size() - 1;
			cnt = g_AICons[index];
			g_AIMtx.unlock();

			if (cnt < MAX_CONS_PER_AI) {
				aidx = index;
				matched = true;
			}
		}

		if (matched == true ) {
			for (int retry = 0; retry < 3; retry++) {
				if (_sndSock == INVALID_SOCKET) {
					_sndSock = socket(AF_INET, SOCK_STREAM, 0);
				}

				if (_sndSock != INVALID_SOCKET) {
					if (Connect(_sndSock, (char*)g_AIIPs[index].c_str(), 9002, 1) == false) {
						closesocket(_sndSock);
						_sndSock = INVALID_SOCKET;
					}
					else {
						uint8_t data = 255;
						ret = ::recv(_sndSock, (char*)&data, 1, 0);
						if (ret == 1) {
							if (data == 0) {
								if (index == g_AICons.size() - 1) {
									g_AIMtx.lock();
									++g_AICons[index];
									cout << "sendS[" << to_string(index) << "] = " << to_string(g_AICons[index]) << ", " << to_string(g_AIRecvCons[index]) << endl;
//									cout << "send[" << to_string(index) << "] = " << to_string(g_AICons[index]) << endl;
									g_AIMtx.unlock();
								}
								_bConnected = true;
								retval = true;
							}
						}
						else { 
							closesocket(_sndSock);   
							_sndSock = INVALID_SOCKET;
						}
						break;
					}
				}
			}
			                         
			if (retval == false) {				//R2가 죽어있어서 연결이 실패하면, Master에게 접속 카운트 증가한거 감소 요청
				SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
				if (sock != INVALID_SOCKET) {
					if (::connect(sock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint)) == SOCKET_ERROR) {   
						closesocket(sock);
						sock = INVALID_SOCKET;
					}
				}

				if (sock != INVALID_SOCKET) {
					TTCPPacketHeader header;
					::memcpy(&header.code, TCPSessionParser::PACKET_MAGIC_CODE, 4);
					header.contentLength = sizeof(CmdHeaderII);

					CmdHeaderII data;
					::memset(&data, 0x00, sizeof(data));
					data.cmd = 1;
					data.category = _IndexAI + 1;

					if (::send(sock, (const char*)&header, sizeof(header), 0) != sizeof(header)) {
						closesocket(sock);
						sock = INVALID_SOCKET;
					}
				}

				if (sock != INVALID_SOCKET) {
					if (::send(sock, (const char*)&data, sizeof(data), 0) != sizeof(data)) {
						closesocket(sock);
						sock = INVALID_SOCKET;
					}
				}

				if (sock != INVALID_SOCKET) {
					TTCPPacketHeader headerR;
					ret = recv(sock, (char*)&headerR, sizeof(TTCPPacketHeader), 0);
					if (ret != sizeof(TTCPPacketHeader)) {
						closesocket(sock);
						sock = INVALID_SOCKET;
					}
					else {
						if (headerR.code[0] != 'T' || headerR.code[1] != 'C' || headerR.code[2] != 'R' || headerR.code[3] != 'S') {
							closesocket(sock);
							sock = INVALID_SOCKET;
						}
					}
				}

				if (sock != INVALID_SOCKET) {
					CmdHeaderII ReplyHeader;
					recv(sock, (char*)&ReplyHeader, sizeof(CmdHeaderII), 0);
					closesocket(sock);
					sock = INVALID_SOCKET;
				}
			}
		}
	}
	else {
		index = 0;

		g_AIMtx.lock();
		cnt = g_AICons[index];

		if (g_AICons.size() > 1) {
			for (int i = 1; i < g_AICons.size() - 1; i++) {
				if (cnt > g_AICons[i]) {
					cnt = g_AICons[i];
					index = i;
				}
			}

			if (cnt >= MAX_CONS_PER_AI) {
				index = g_AICons.size() - 1;
				cnt = g_AICons[index];
			}
		}
//		g_AIMtx.unlock();
		cout << "index : " << index << ", cnt : " << int(cnt) << endl;
		if (cnt < MAX_CONS_PER_AI) {
			aidx = index;
			for (int retry = 0; retry < 3; retry++) {
				if (_sndSock == INVALID_SOCKET) {
					_sndSock = socket(AF_INET, SOCK_STREAM, 0);

/*
					ULONG nonBlk = TRUE;
					if (ioctlsocket(_sndSock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
						closesocket(_sndSock);
						_sndSock = INVALID_SOCKET;
					}
*/
				}

				if (_sndSock != INVALID_SOCKET) {
					if (Connect(_sndSock, (char*)g_AIIPs[index].c_str(), 9002, 1) == false) {
						closesocket(_sndSock);
						_sndSock = INVALID_SOCKET;
					}
					else {
						uint8_t data = 255;
						int ret = ::recv(_sndSock, (char*)&data, 1, 0);
						if (ret == 1) {
							if (data == 0) {
//								g_AIMtx.lock();
								++g_AICons[index];
								if (index < g_AICons.size() - 1)
									++g_IoSCons[make_pair(_curIP, index)];
								cout << "send[" << to_string(index) << "] = " << to_string(g_AICons[index]) << ", " << to_string(g_AIRecvCons[index]) << endl;
//								cout << "send[" << to_string(index) << "] = " << to_string(g_AICons[index]) << endl;
//								g_AIMtx.unlock();
								_bConnected = true;
								retval = true;
							}
							else {
								closesocket(_sndSock);
								_sndSock = INVALID_SOCKET;
							}
						}
						break;
					}
				}
//				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}
		g_AIMtx.unlock();
	}

	if (retval) {
		_IndexAI = index;
		g_AIMtx.lock();
		cnt = g_AICons[index];
		g_AIMtx.unlock();
		SetTransferCount(cnt);
	}
	else {
		_IndexAI = -1;
	}

	return retval;
}

void DeviceStream::DisconnectAI()
{
	bool bMaster = false;

	if (_bConnected) {
		if (_IndexAI == g_AICons.size() - 1) {
			g_AIMtx.lock();
			if (g_AICons[_IndexAI] > 0 && g_AICons[_IndexAI] <= MAX_CONS_PER_AI)
				--g_AICons[_IndexAI];
			cout << "Dsend1[" << to_string(_IndexAI) << "] = " << to_string(g_AICons[_IndexAI]) << endl;
			g_AIMtx.unlock();
		}
		else {
			std::unique_lock<std::mutex> lock(g_IoSMtx);
			bMaster = g_bMaster;
			lock.unlock();

			if (bMaster == false) {
				sockaddr_in hint;
				hint.sin_family = AF_INET;
				hint.sin_port = htons(9000);
				int ret = inet_pton(AF_INET, g_IoSLists[g_nMasterInx].c_str(), &hint.sin_addr);
				if (ret != 1)
					return;

				SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
				if (sock == INVALID_SOCKET)
					return;

				ret = ::connect(sock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint));
				if (ret == SOCKET_ERROR) {
					closesocket(sock);
					sock = INVALID_SOCKET;
					return;
				}

				TTCPPacketHeader header;
				::memcpy(&header.code, TCPSessionParser::PACKET_MAGIC_CODE, 4);
				header.contentLength = sizeof(CmdHeaderII);

				CmdHeaderII data;
				::memset(&data, 0x00, sizeof(data));
				data.cmd = 1;
				data.category = _IndexAI + 1;

				if (::send(sock, (const char*)&header, sizeof(header), 0) != sizeof(header)) {
					closesocket(sock);
					sock = INVALID_SOCKET;
					return;
				}

				if (::send(sock, (const char*)&data, sizeof(data), 0) != sizeof(data)) {
					closesocket(sock);
					sock = INVALID_SOCKET;
					return;
				}

				TTCPPacketHeader headerR;
				ret = recv(sock, (char*)&headerR, sizeof(TTCPPacketHeader), 0);
				if (ret != sizeof(TTCPPacketHeader)) {
					closesocket(sock);
					sock = INVALID_SOCKET;
					return;
				}

				if (headerR.code[0] != 'T' || headerR.code[1] != 'C' || headerR.code[2] != 'R' || headerR.code[3] != 'S') {
					closesocket(sock);
					sock = INVALID_SOCKET;
					return;
				}

				CmdHeaderII ReplyHeader;
				ret = recv(sock, (char*)&ReplyHeader, sizeof(CmdHeaderII), 0);
				if (ret != sizeof(CmdHeaderII)) {
					closesocket(sock);
					sock = INVALID_SOCKET;
					return;
				}

				closesocket(sock);
				sock = INVALID_SOCKET;
			}
			else {
				g_AIMtx.lock();
				if (g_AICons[_IndexAI] > 0 && g_AICons[_IndexAI] <= MAX_CONS_PER_AI) {
					--g_AICons[_IndexAI];
				}
//				else {
//					g_AICons[_IndexAI] = 255;		//kimdh0716 - r2 down,reboot error
//				}
				--g_IoSCons[make_pair(_curIP, _IndexAI)];
				cout << "Dsend2[" << to_string(_IndexAI) << "] = " << to_string(g_AICons[_IndexAI]) << endl;
				g_AIMtx.unlock();
			}
		}
		_bConnected = false;

		if (_sndSock != INVALID_SOCKET) {
			closesocket(_sndSock);
			_sndSock = INVALID_SOCKET;
		}

//		cout << "Discon = " << to_string(_ch_seq) << endl;
	}

	_IndexAI = -1;
}

void DeviceStream::SetTransferCount(int count)
{
#if 1
	if (count <= 15)
		_TransCount = 10;
	else if (count <= 19)
		_TransCount = 8;
	else if (count <= 31)
		_TransCount = 5;
	else if (count <= 39)
		_TransCount = 4;
	else
		_TransCount = 2;
#else
	if (count <= MAX_CONS_PER_AI / 8)
		_TransCount = 4;
	else if (count <= MAX_CONS_PER_AI / 4)
		_TransCount = 3;
	else
		_TransCount = 2;
#endif
}

void DeviceStream::PushSendData(uint8_t* pData)
{
	std::unique_lock<std::mutex> lock(_mtxSnd);
	bool bNotify = false;
	if (_listSnd.empty())
		bNotify = true;
	_listSnd.push_back(pData);
	if (bNotify)
		_condSnd.notify_one();
}

uint8_t* DeviceStream::MakeSendData(VideoStreamData* pStream, bool bDetected)
{
	uint8_t* data = new uint8_t[sizeof(VideoHeader) + pStream->streamDataSize];

	uint8_t* buf2 = data + sizeof(VideoHeader);
	VideoHeader* pHeader = (VideoHeader*)data;
	pHeader->camera_seq = _ch_seq;
	pHeader->codec_type = pStream->streamDataType;
	pHeader->frame_type = pStream->frameType;
	pHeader->st = pStream->streamTime;
	pHeader->bDetected = bDetected;
	pHeader->buf_size = pStream->streamDataSize;
	::memcpy(buf2, pStream->streamData, pStream->streamDataSize);

	return data;
}

bool DeviceStream::UpdateAIState(int index, bool bState)
{


	if (index < 0 || index >= g_AIIPs.size())
		return false;

	if (bState == false) {
		CString strName;
		std::string sIP;
		if (index == g_AIIPs.size() - 1) {
			strName = L"Inner R2";
			sIP = _curIP;
		}
		else {
			strName = g_AINames[index];
			sIP = g_AIIPs[index];
		}

		std::string query = "INSERT INTO device_err_mgr(device_type, device_nm, device_ip, detct_device_nm, detct_device_ip, err_code, err_msg) VALUES('iot', '" + std::string(CT2CA(strName)) + "', '" + sIP + "', '" + std::string(CT2CA(g_strIoSName)) + "', '" + _curIP + "', '";

		CString strComment = L"NC', '연결 실패')";

		query += std::string(CT2CA(strComment));

		if (_evtIP.empty())
			return false;

		CDBPostgre* p_DbPostgre = new CDBPostgre(_evtIP);
		if (!p_DbPostgre)
			return false;

		if (!p_DbPostgre->_pDB) {
			delete p_DbPostgre;
			return false;
		}
		else {
			ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
			if (connstatus == CONNECTION_BAD) {
				p_DbPostgre->_pDB->CloseConnection();
				delete p_DbPostgre;
				return false;
			}
		}

		p_DbPostgre->dbInsert(query);

		p_DbPostgre->_pDB->CloseConnection();

		delete p_DbPostgre;
	}

	return true;
}

BOOL DeviceStream::FindDir(CString sDir)
{
	CFileFind file;
	CString strFile = _T("*.*");
	BOOL bResult = file.FindFile(sDir + strFile);
	file.Close();
	return bResult;
}

void DeviceStream::CreateFolder(CString sPath)
{
	sPath.Replace(_T("\\"), _T("/"));
	CString sToken;
	CString sFolder = _T("");
	int nIndex = 0;
	while (TRUE == AfxExtractSubString(sToken, sPath, nIndex, '/'))
	{
		sFolder += (sToken + _T("/"));
		if (FALSE == FindDir(sFolder))
		{
			if (FALSE == CreateDirectory(sFolder, NULL))
			{
			}
		}
		nIndex++;
	}
}

bool DeviceStream::SendEvent(std::string ch_id, int ch_seq, SYSTEMTIME Time, string ipaddr, Mat& frame)
{
	int algId = 0;
	std::vector<CString> sCmds;

	CString sPath;
	sPath.Format(_T("%s\\%04d\\%02d\\%02d\\%08d"), FILE_EVENT_PATH, Time.wYear, Time.wMonth, Time.wDay, ch_seq);
	CreateFolder(sPath);

	//이미지 저장
	CString sTemp;
	sTemp.Format(_T("\\%04d%02d%02d%02d%02d%02d%03d_alg%02d.jpg"), Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds, algId);
	CString sFile = sPath + sTemp;
	std::string filename = std::string(CT2CA(sFile));


	bool writeRet = cv::imwrite(filename.c_str(), frame);

	if (writeRet == false)
		return false;

	//이벤트(ALARM) DB 저장
	char path[128];
	::sprintf(path, "%04d/%02d/%02d/%08d/%04d%02d%02d%02d%02d%02d%03d_alg%02d.jpg", Time.wYear, Time.wMonth, Time.wDay, ch_seq, Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds, algId);

#ifdef _DIRECT_DB
	CString sCmd = _T("INSERT INTO \"CURATION_ALARMS\" (curation_alarm_id, camera_id, cameras_seq, svr_id, file_path, alarm_type, occurrence_date) VALUES (%d, '") + CString(ch_id.c_str()) + _T("', '") + CString(to_string(ch_seq).c_str()) + _T("', '") + CString(_curIP.c_str()) + _T("', '") + CString(path) + _T("', 'alg00', '");
	CString sTime;
	sTime.Format(_T("%04d-%02d-%02d %02d:%02d:%02d.%03d')"), Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds);

	sCmd += sTime;
	sCmds.push_back(sCmd);
#else
	Json::Value CurationAlarm;
	CurationAlarm["camera_id"] = ch_id;
	CurationAlarm["cameras_seq"] = ch_seq;
	CurationAlarm["file_path"] = path;
	CurationAlarm["alarm_type"] = std::string("alg") + ((algId >= 10) ? std::to_string(algId) : (std::string("0") + std::to_string(algId)));
	CurationAlarm["svr_id"] = ipaddr;

	Json::Value rootAlarm;
	rootAlarm["CURATION_ALARMS"] = CurationAlarm;
	Json::StreamWriterBuilder builder;
	std::string cmds = Json::writeString(builder, rootAlarm);
	for (int i = 0; i < cmds.length(); i++) {
		if (cmds[i] == '\n')
			cmds[i] = ' ';
	}

	sCmds.push_back(CString(cmds.c_str()));
#endif

	if (_pEvtMgr)
		_pEvtMgr->pushEventCmd(sCmds);

	return true;
}

bool DeviceStream::UpdateCameraState(int state)
{
	std::string query = "INSERT INTO device_err_mgr(device_type, device_nm, device_ip, detct_device_nm, detct_device_ip, device_seq, err_code, err_msg) VALUES('camera', '" + std::string(CT2CA(_strChannelName)) + "', '" + _hostName + "', '" + std::string(CT2CA(g_strIoSName)) + "', '" + _curIP + "', '" + to_string(_ch_seq) + "', '";

	CString strComment;
	if (state == 1)
		strComment = L"NS', '장비로부터의 응답이 없습니다')";
	else if (state == 2)
		strComment = L"NC', '연결 실패')";
	else
		return false;

	query += std::string(CT2CA(strComment));
	
	if (_evtIP.empty())
		return false;

	CDBPostgre* p_DbPostgre = new CDBPostgre(_evtIP);
	if (!p_DbPostgre)
		return false;

	if (!p_DbPostgre->_pDB) {
		delete p_DbPostgre;
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			p_DbPostgre->_pDB->CloseConnection();
			delete p_DbPostgre;
			return false;
		}
	}
	//cout<< query<<endl;
	p_DbPostgre->dbInsert(query);
	p_DbPostgre->_pDB->CloseConnection();

	delete p_DbPostgre;
	return true;
}

bool DeviceStream::UpdateErrorMgr()		//kimdh0813 - 고장처리
{

	std::string query = "UPDATE \"CAMERAS\" set err_stat = 0 where camera_name = '" + std::string(CT2CA(_strChannelName)) + "' and ip = '" + _hostName + "'";
	//cout << "query : " << query << endl;
	if (_evtIP.empty())
		return false;

	CDBPostgre* p_DbPostgre = new CDBPostgre(_evtIP);
	if (!p_DbPostgre)
		return false;

	if (!p_DbPostgre->_pDB) {
		delete p_DbPostgre;
		return false;
	}
	else {
		ConnStatusType connstatus = PQstatus(p_DbPostgre->_pDB->m_conn);
		if (connstatus == CONNECTION_BAD) {
			p_DbPostgre->_pDB->CloseConnection();
			delete p_DbPostgre;
			return false;
		}
	}

	p_DbPostgre->dbInsert(query);

	p_DbPostgre->_pDB->CloseConnection();

	delete p_DbPostgre;


	return true;
}

void DeviceStream::ResetCameraState()
{
	if (_CamInfo.status == 1)
		_CamInfo.status = 0;
}

void DeviceStream::SetCamInfo(ChInfo_t camInfo)
{
	_CamInfo_copy = camInfo;
	_bChangeInfo = true;
}

bool DeviceStream::IsIdleState() const
{
	return _bIdleState;
}

bool DeviceStream::Connect(SOCKET sock, char* host, int port, int timeout)
{
	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, host, &addr.sin_addr);

	ULONG nonBlk = TRUE;
	if (ioctlsocket(sock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
		return false;
	}

	int nRet = ::connect(sock, (const struct sockaddr*)&addr, sizeof(addr));
	if (nRet == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		return false;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET((unsigned int)sock, &fdset);

	struct timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	if (select(0, NULL, &fdset, NULL, &tv) == SOCKET_ERROR) {
		return false;
	}

	if (!FD_ISSET(sock, &fdset)) {
		return false;
	}

	nonBlk = FALSE;
	if (ioctlsocket(sock, FIONBIO, &nonBlk) == SOCKET_ERROR) {
		return false;
	}

	return true;
}
