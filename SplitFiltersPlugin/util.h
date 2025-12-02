#include <windows.h>
#include "plugin2.h"
#include <vector>
#include <string>

// --- 定数/マクロ（エイリアス解析に必要なもの） ---
static const char* GROUP_OBJ0 = u8R"(
[Object.0]
effect.name=グループ制御
X=0.00
Y=0.00
Z=0.00
Group=1
X軸回転=0.00
Y軸回転=0.00
Z軸回転=0.00
拡大率=100.000
対象レイヤー数=1
)";

static const char* GROUP_AUDIO_OBJ0 = u8R"(
[Object.0]
effect.name=グループ制御(音声)
音量=100.00
左右=0.00
対象レイヤー数=1
)";

/// オブジェクトが被っているときに再試行する回数の上限
const int SAFE_LAYER_LIMIT = 1000;

/// パース済みエイリアスデータ
struct ObjSec {
	std::string sec;	// [Object.x] セクションの文字列
	int index;			// [Object.x] の x の部分
	std::string effect_name;
};

HWND get_aviutl2_window();
std::wstring utf8_to_wide(const std::string& s);
std::vector<ObjSec> parse_objects(const std::string& alias);
int calc_start_index(const std::vector<ObjSec>& objs);
std::string rebuild_alias(
	const std::vector<ObjSec>& objs,
	int start_index,
	int base_index
);
std::string build_source_alias(const std::string alias);
std::string build_target_alias(const std::string alias);
std::string build_target_alias_group(const std::string alias);
OBJECT_HANDLE try_create_group(
	EDIT_SECTION* edit,
	const std::string& alias,
	int layer, int start, int length);