#pragma once

#include <windows.h>
#include "plugin2.h"
#include <vector>
#include <string>
#include <unordered_map>

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

static const char* COMPOSITE_OBJ01 = u8R"(
[Object.0]
effect.name=画像合成(オブジェクト)
対象レイヤー数=1
[Object.1]
effect.name=標準描画
X=0.00
Y=0.00
Z=0.00
Group=1
中心X=0.00
中心Y=0.00
中心Z=0.00
X軸回転=0.00
Y軸回転=0.00
Z軸回転=0.00
拡大率=100.000
縦横比=0.000
透明度=0.00
合成モード=通常
)";

static const char* FILTER_OBJECT_OBJ0 = u8R"(
[Object.0]
effect.name=フィルタオブジェクト
)"; 

static const char* GROUP_AUDIO_OBJ0 = u8R"(
[Object.0]
effect.name=グループ制御(音声)
音量=100.00
左右=0.00
対象レイヤー数=1
)";

static const char* OUTPUT_SECTION_LIST[] = {
	u8"標準描画",
	u8"音声再生",
	u8"映像再生",
	u8"基本出力",
	u8"パーティクル出力",
	u8"仮想バッファ出力",
};

static const char* NON_OUTPUT_SECTION_OBJECT_LIST[] = {
	u8"フィルタオブジェクト",
	u8"部分フィルタ",
	u8"グループ制御",
	u8"グループ制御(音声)",
	u8"カメラ制御",
	u8"時間制御(オブジェクト)",
	u8"シーンチェンジ",
};

/// オブジェクトが被っているときに再試行する回数の上限
const int SAFE_LAYER_LIMIT = 1000;

/// パース済みエイリアスデータ
struct ObjSec {
	std::string sec;	// [Object.x] セクションの文字列
	int index;			// [Object.x] の x の部分
	std::string effect_name;
	std::unordered_map<std::string, std::string> props;	// パラメータマップ
};

/// 基本効果パラメータのマッピング定義 (元キー → 出力キー)
struct PropMapping {
	std::string src_key;		// 元セクション内のキー名
	std::string dst_key;		// 出力フィルタ内のキー名
	std::string default_val;	// デフォルト値（この値なら分離しない）
};

struct BasicEffectDef;
/// カスタムビルダー: 転記以外でのフィルタを生成する関数型 (縦横比→拡大率のようなもの)
typedef bool (*CustomBuildFunc)(const ObjSec& src, const BasicEffectDef& def, ObjSec& out);
/// カスタムリセッター: 転記以外でのリセットを行う関数型 (縦横比→拡大率のようなもの)
typedef void (*CustomResetFunc)(ObjSec& src, const BasicEffectDef& def);

/// 基本効果フィルタの定義
struct BasicEffectDef {
	std::string effect_name;				// フィルタ効果名
	std::vector<PropMapping> mappings;		// パラメータマッピング一覧
	CustomBuildFunc custom_build;			// カスタムビルダー (nullptr なら通常マッピング)
	CustomResetFunc custom_reset;			// カスタムリセッター (nullptr なら通常リセット)
};
extern const std::vector<BasicEffectDef> BASIC_EFFECT_DEFS;

HWND get_aviutl2_window();
std::wstring utf8_to_wide(const std::string& s);
std::string extract_object_header(const std::string& alias);
std::vector<ObjSec> parse_objects(const std::string& alias);
int calc_start_index(const std::vector<ObjSec>& objs, bool include_self_filter = false);
bool has_output_section(const std::vector<ObjSec>& objs);
bool is_none_output_object(const std::vector<ObjSec>& objs);
std::string rebuild_alias(
	const std::vector<ObjSec>& objs,
	int start_index,
	int base_index
);
std::string build_source_alias(const std::string alias);
std::string build_target_alias(const std::string alias);
std::vector<ObjSec> build_basic_effect_filters(const std::vector<ObjSec>& objs);
void reset_basic_effect_params(std::vector<ObjSec>& objs, const std::vector<ObjSec>& filters);
bool get_prop_as_double(const ObjSec& obj, const std::string& key, double& out);\
bool is_prop_default(const ObjSec& obj, const std::string& key, const std::string& default_val);
OBJECT_HANDLE try_create_group(
	EDIT_SECTION* edit,
	const std::string& alias,
	int layer, int start, int length);
OBJECT_HANDLE try_create_composite(
	EDIT_SECTION* edit,
	const std::string& alias,
	int layer, int start, int length);
int find_available_layer(EDIT_SECTION* edit, int start_layer, int start_frame, int end_frame);