#include "TsuWebSocketPrivatePCH.h"
#include "TsuWebSocketModule.h"
#include "TsuWebSocketLog.h"

#if !UE_BUILD_SHIPPING
static void lws_debugLog(int level, const char* line)
{
	UE_LOG(LogTsuWebSocket, Log, TEXT("websocket server: %s"), ANSI_TO_TCHAR(line));
}
#endif 

class FTsuWebSocketModule final : public ITsuWebSocketModule
{
public:
	void StartupModule() override
	{
#if !UE_BUILD_SHIPPING
		int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;
			/* for LLL_ verbosity above NOTICE to be built into lws,
			 * lws must have been configured and built with
			 * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
			 /* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
			 /* | LLL_EXT */ /* | LLL_CLIENT */ /* | LLL_LATENCY */
			/* | LLL_DEBUG */;

		lws_set_log_level(logs, lws_debugLog);
#endif 
	}

	void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FTsuWebSocketModule, TsuWebSocket)
