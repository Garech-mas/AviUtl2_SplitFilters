#include "util.h"
#include "logger2.h"

// --- 外部変数宣言 ---
extern EDIT_HANDLE* edit_handle;
extern LOG_HANDLE* logger;


// --- プラグイン情報定数 ---
#define PLUGIN_NAME L"フィルタ分離"
#define PLUGIN_VERSION L"v1.00"
#define TESTED_BETA L"beta22"
#define PLUGIN_INFO PLUGIN_NAME " " PLUGIN_VERSION " (テスト済: " TESTED_BETA ") by Garech"


// --- DLLエクスポート関数宣言 ---
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle);
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host);