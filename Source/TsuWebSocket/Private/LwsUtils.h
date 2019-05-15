#pragma once

const TCHAR* LwsStatusName(int Code);
const FString LwsGetHeader(lws* Wsi, lws_token_indexes H);
int LwsGetHeader(lws* Wsi, lws_token_indexes H, int Default);
int LwsWrite(lws* Wsi, const TCHAR* Data, lws_write_protocol H);
