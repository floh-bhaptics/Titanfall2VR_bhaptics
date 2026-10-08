#ifndef BHAPTICS_LIBRARY_H
#define BHAPTICS_LIBRARY_H



#if defined _WIN32 || defined __CYGWIN__
#ifdef BHAPTICS_BUILDING_DLL
#ifdef __GNUC__
      #define DLL_PUBLIC __attribute__ ((dllexport))
    #else
      #define DLL_PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
#else
#ifdef __GNUC__
#define DLL_PUBLIC __attribute__ ((dllimport))
#else
#define DLL_PUBLIC __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
#endif
#endif
#define DLL_LOCAL
#else
#if __GNUC__ >= 4
#define DLL_PUBLIC __attribute__ ((visibility ("default")))
#define DLL_LOCAL  __attribute__ ((visibility ("hidden")))
#else
#define DLL_PUBLIC
    #define DLL_LOCAL
#endif
#endif



#ifdef __cplusplus
extern "C" {  // only need to export C interface if
// used by C++ source code
#endif

/**
 *
 *        @brief 플레이어에 앱 등록
 *        @details 플레이어에 앱 등록을 위한 함수로, developer 사이트에서 생성한 workspace를 연결할 수 있다.
 *        @param const char* sdkAPIKey 해당 workspace의 key
 *        @param const char* workspaceId 해당 app의 workspace ID
 *        @param const char* initJsonMessage 초기 햅틱 메시지 Json 형태의 string 값
 *        @return boolean 웹소켓이 열리면 true, 아니면 false
 *
 */
DLL_PUBLIC bool registryAndInit(const char* sdkAPIKey, const char* workspaceId, const char* initJsonMessage);

/**
 *
 *        @brief 외부 플레이어에 앱 등록
 *        @details registryAndInit와 같이 플레이어에 앱을 등록하는 함수로, 로컬이 아닌 외부에 실행된 플레이어에 연결을 시도한다.
 *        @param const char* sdkAPIKey 해당 workspace의 key
 *        @param const char* workspaceId 해당 app의 workspace ID
 *        @param const char* initJsonMessage 초기 햅틱 메시지 Json 형태의 string 값
 *        @param const url* 연결하고자 하는 외부 IP
 *        @return boolean 웹소켓이 열리면 true, 아니면 false
 *
 */
DLL_PUBLIC bool registryAndInitHost(const char* sdkAPIKey, const char* workspaceId, const char* initJsonMessage, const char* url);

/**
 *
 *        @brief 웹소켓 연결 상태 확인
 *        @details 웹소켓이 플레이어와 연결된 상태인지를 확인한다.
 *        @return boolean 플레이어와 연결이 되어있으면 true, 아니면 false
 *
 */
DLL_PUBLIC bool wsIsConnected();

/**
 *
 *        @brief 웹소켓 연결 종료
 *        @details 웹소켓의 연결을 종료시킨다.
 *        @return void
 *
 */
DLL_PUBLIC void wsClose();

/**
 *
 *        @brief 플레이어에 앱 재등록
 *        @details 플레이어에 앱 재등록을 위한 함수로, developer 사이트에서 생성한 workspace를 연결할 수 있다.
 *        @param const char* sdkAPIKey 해당 workspace의 key
 *        @param const char* workspaceId 해당 app의 workspace ID
 *        @param const char* initJsonMessage 초기 햅틱 메시지 Json 형태의 string 값
 *        @return boolean 메시지가 정상적으로 송신되었으면 true, 웹소켓 연결이 끊어져 있으면 false
 *
 */
DLL_PUBLIC bool reInitMessage(const char* sdkAPIKey, const char* workspaceId, const char* initJsonMessage);

/**
 *
 *        @brief 이벤트 재생
 *        @details 이벤트 재생을 위한 함수로 workspace에 정의한 event를 재생시킨다.
 *        @param const char* key 정의한 Event Name
 *        @return int requestId
 *
 */
DLL_PUBLIC int play(const char* key);

/**
 *
 *        @brief 이벤트 재생
 *        @details 파라미터를 통한 이벤트 재생
 *        @param const char* key 정의한 Event Name
 *        @param int requestId 값이 0일 경우에는 내부에서 생성해 줌
 *        @param float intensity 세기 (Default = 1.0)
 *        @param float duration 지속시간 (Default = 1.0)
 *        @param float angleX X각도 Offset (Default = 0.0)
 *        @param float offsetY Y각도 Offset (Default = 0.0)
 *        @return int requestId
 *
 */
DLL_PUBLIC int playParam(const char* key, int requestId, float intensity, float duration, float angleX, float offsetY);

/**
 *
 *        @brief 리턴값 없는 이벤트 재생
 *        @details 리턴값 없는 이벤트 재생
 *        @param const char* key 정의한 Event Name
 *        @param int requestId 값이 0일 경우에는 내부에서 생성해 줌
 *        @param float intensity 세기 (Default = 1.0)
 *        @param float duration 지속시간 (Default = 1.0)
 *        @param float angleX X각도 Offset (Default = 0.0)
 *        @param float offsetY Y각도 Offset (Default = 0.0)
 *
 */
DLL_PUBLIC void playWithoutResult(const char* key, int requestId, float intensity, float duration, float angleX, float offsetY);

 /**
  *
  *        @brief
  *        @details
  *        @param const char* key 정의한 Event Name
  *        @param int requestId 값이 0일 경우에는 내부에서 생성해 줌
  *        @param int startMillis 이벤트를 시작할 위치 ms초
  *        @param float intensity 세기 (Default = 1.0)
  *        @param float duration 지속시간 (Default = 1.0)
  *        @param float angleX X각도 Offset (Default = 0.0)
  *        @param float offsetY Y각도 Offset (Default = 0.0)
  *        @return int requestId
  *
  */
DLL_PUBLIC int playWithStartTime(const char* key, int requestId, int startMillis, float intensity, float duration, float angleX, float offsetY);

/**
 *
 *        @brief DotMode 실행
 *        @details DotMode 실행
 *        @param int requestId 값이 0일 경우에는 내부에서 생성해 줌
 *        @param int position 재생을 원하는 bHaptics Product
 *        @param int duration 지속시간 (Default = 1)
 *        @param int* motorValues 모터 위치의 Intensity (0~100)
 *        @param int motorValueLen motorValues의 배열 길이 (0~32)
 *        @return int requestId
 *
 */
DLL_PUBLIC int playDot(int requestId, int position, int duration, int* motorValues, int motorValueLen);

/**
*
*        @brief playWaveform 실행
*        @details playWaveform 실행
*        @param int requestId 값이 0일 경우에는 내부에서 생성해 줌
*        @param int position 재생을 원하는 bHaptics Product
*        @param int* motorValues 모터 위치의 Intensity (0~100)
*        @param int* playTimeValues 모터 위치의 playTime (5ms 간격, 1:5ms, 2:10ms, 4:20ms, 6:30ms, 8:40ms)
*        @param int* shapeValues 모터 위치의 shape (0: 유지, 1:50% 리니어 하강, 2:50% 리니어 상승)
*        @param int repeatCount 반복할 횟수
*        @param int motorLen 모터 길이 (6)
*        @return int requestId
*
*/
DLL_PUBLIC int playWaveform(int requestId, int position, int* motorValues, int* playTimeValues, int* shapeValues, int repeatCount, int motorLen);

/**
*
*        @brief playPath 실행
*        @details playPath 실행
*        @param int requestId 값이 0일 경우에는 내부에서 생성해 줌
*        @param int position 재생을 원하는 bHaptics Product
*        @param float duration 지속시간 (Default = 1.0)
*        @param float* xValues x좌표 배열
*        @param float* yValues y좌표 배열
*        @param int* intensity 좌표의 세기 배열
*        @param int len 모터 길이(0~32)
*        @return int requestId
*
*/
DLL_PUBLIC int playPath(int requestId, int position, int durationMillis, float* xValues, float* yValues, int* intensityValues, int Len);

/**
 *
 *        @brief Loop Event
 *        @details Loop Event 실행
 *        @param const char* key 정의한 Event Name
 *        @param int requestId 값이 0일 경우에는 내부에서 생성해 줌
 *        @param float intensity 세기 (Default = 1.0)
 *        @param float duration 지속시간 (Default = 1.0)
 *        @param float angleX X각도 Offset (Default = 0.0)
 *        @param float offsetY Y각도 Offset (Default = 0.0)
 *        @param int interval 반복 될 때의 Interval (Default = 0)
 *        @param int maxCount 반복할 횟수 (Default = 0)
 *        @return int requestId
 *
 */
DLL_PUBLIC int playLoop(const char* eventId, int requestId, float intensity, float duration, float angleX, float offsetY, int interval, int maxCount);

/**
 *
 *        @brief Event의 Event 작동 시간
 *        @details Event의 Event 작동 시간
 *        @param const char* eventId 정의한 Event Name
 *        @return int event time (ms)
 *
 */
DLL_PUBLIC int getEventTime(const char* eventId);

 /**
 *
 *        @brief 일시 정지
 *        @details 실핼 중인 이벤트를 일시 정지함
 *        @param const char* eventId 일시정지 할 이벤트 id
 *        @return int 일시정지된 시점의 ms초
 *
 */
DLL_PUBLIC int pause(const char* eventId);

 /**
 *
 *        @brief 이벤트 재개
 *        @details 일시 정지된 이벤트를 재개함
 *        @param const char* eventId 재개 할 이벤트 id
 *        @return boolean 재개 성공 여부
 *
 */
DLL_PUBLIC bool resume(const char* eventId);

/**
 *
 *        @brief Event 종료
 *        @details RequestId로 Event 종료
 *        @param int requestKey play시 리턴 받은 requestId
 *        @return boolean 성공 시 true, 아니면 false
 *
 */
DLL_PUBLIC bool stop(int requestKey);

/**
 *
 *        @brief Event 종료
 *        @details EventId로 Event 종료
 *        @param const char* 종료하고자 하는 EventId
 *        @return boolean 성공 시 true, 아니면 false
 *
 */
DLL_PUBLIC bool stopByEventId(const char* eventId);

/**
 *
 *        @brief Event 전체 종료
 *        @details 실행되고 있는 전체 Event 종료
 *        @return boolean 성공 시 true, 아니면 false
 *
 */
DLL_PUBLIC bool stopAll();

/**
 *
 *        @brief 특정 기기 연결 상태 확인
 *        @details 특정 기기가 연결되어 있는 지 확인
 *        @param int position 기기 Production
 *        @return boolean 연결되어 있으면 true, 아니면 false
 *
 */
DLL_PUBLIC bool isbHapticsConnected(int position);

/**
 *
 *        @brief 실행 중 Event 여부 확인
 *        @details 실행되고 있는 Event 확인
 *        @return boolean 실행 되고 있는 Event가 하나라도 있으면 true, 아니면 false
 *
 */
DLL_PUBLIC bool isPlaying();

/**
 *
 *        @brief 실행 중 Event 여부 확인
 *        @details requestId로 실행 되고 있는 Event 확인
 *        @param int requestKey play시 리턴 받은 requestId
 *        @return boolean Event가 실행 되고 있으면 true, 아니면 false
 *
 */
DLL_PUBLIC bool isPlayingByRequestId(const int requestId);

/**
 *
 *        @brief 실행 중 Event 여부 확인
 *        @details Event name으로 실행 되고 있는 Event 확인
 *        @param const char* eventId Event Name
 *        @return boolean Event가 실행 되고 있으면 true, 아니면 false
 *
 */
DLL_PUBLIC bool isPlayingByEventId(const char* eventId);

DLL_PUBLIC const char* bHapticsGetHapticMessage(const char* appKey, const char* workspaceId, int lastVerison, int& status);
DLL_PUBLIC const char* bHapticsGetHapticMappings(const char* appKey, const char* workspaceId, int lastVerison, int& status);

/**
 *
 *        @brief 플레이어 실행 여부 확인
 *        @details 플레이어가 실행 되고 있는 지 확인
 *        @return boolean 플레이어가 실행 되고 있으면 true, 아니면 false
 *
 */
DLL_PUBLIC bool isPlayerRunning();

/**
 *
 *        @brief 플레이어 설치 여부 확인
 *        @details 플레이어가 설치 되고 있는 지 확인
 *        @return boolean 플레이어가 설치 되어 있으면 true, 아니면 false
 *
 */
DLL_PUBLIC bool isPlayerInstalled();

/**
 *
 *        @brief 플레이어 실행
 *        @details 플레이어를 실행하기 위한 함수
 *        @param bool tryLaunch 실행 시킬 지 여부
 *        @return boolean 플레이어가 실행 되면 true, 아니면 false
 *
 */
DLL_PUBLIC bool launchPlayer(bool tryLaunch);

/**
 *
 *        @brief 연결된 기기 정보
 *        @details 연결된 기기 정보
 *        @return const char* 연결된 기기 정보를 json 형태의 string
 *
 */
DLL_PUBLIC const char* getDeviceInfoJson();

/**
 *
 *        @brief 햅틱 이벤트 리스트
 *        @details 해당 App의 Haptic Event List
 *        @return const char* 해당 App의 Haptic Event List를 json 형태의 string
 *
 */
DLL_PUBLIC const char* getHapticMappingsJson();

/**
 *
 *        @brief 기기 핑
 *        @details 플레이어에 연결된 특정 기기에 핑을 보냄
 *        @param const char* 기기의 MacAddress
 *        @return boolean 메시지가 전송되었으면 true, 플레이어와 연결이 안되어 있으면 false
 *
 */
DLL_PUBLIC bool ping(const char* address);

/**
 *
 *        @brief 기기 핑
 *        @details 플레이어에 연결된 모든 기기에 핑을 보냄
 *        @return boolean 메시지가 전송되었으면 true, 플레이어와 연결이 안되어 있으면 false
 *
 */
DLL_PUBLIC bool pingAll();

/**
 *
 *        @brief 기기 좌우 변경
 *        @details 좌우를 변경할 수 있는 기기의 좌우를 변경하기 위한 함수
 *        @param const char* 기기의 MacAddress
 *        @return boolean 좌우가 바뀌면 true, 아니면 false
 *
 */
DLL_PUBLIC bool swapPosition(const char* address);

/**
 *
 *        @brief 기기 VSM 변경
 *        @details 기기의 VSM를 변경하기 위한 함수
 *        @param const char* 기기의 MacAddress
 *        @param const int 설정할 VSM (0 ~ 400)
 *        @return boolean 좌우가 바뀌면 true, 아니면 false
 *
 */
DLL_PUBLIC bool setDeviceVsm(const char* address, int vsm);

#ifdef __cplusplus
}
#endif
#endif //BHAPTICS_LIBRARY_H