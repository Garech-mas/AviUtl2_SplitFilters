#include "util.h"

/// AviUtl2 のメインウィンドウを取得する
HWND get_aviutl2_window() {
	const std::wstring className = L"aviutl2Manager";
	DWORD currentPid = GetCurrentProcessId();
	HWND hWnd = nullptr;

	while ((hWnd = FindWindowExW(nullptr, hWnd, className.c_str(), nullptr)) != nullptr) {
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hWnd, &windowPid);

		if (windowPid == currentPid) {
			return hWnd; // 最初に見つかったものを返す
		}
	}

	return nullptr; // 見つからなかった場合
}


/// UTF-8のstd::stringを、std::wstringに変換する
std::wstring utf8_to_wide(const std::string& s) {
	size_t size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), s.size(), NULL, 0);
	std::wstring result(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), s.size(), &result[0], size);
	return result;
}


/// [Object] ヘッダを抜き出す
/// @param alias エイリアスデータ
/// @return [Object] ～ [Object.0] の直前までのエイリアスデータ
std::string extract_object_header(const std::string& alias) {
	size_t header_start = alias.find("[Object]");
	if (header_start == std::string::npos) return "";

	size_t obj0_start = alias.find("[Object.0]");
	if (obj0_start == std::string::npos) return "";

	return alias.substr(header_start, obj0_start - header_start);
}


/// 指定された位置が行頭であるかを判定する
/// @param head 判定を行うインデックス
/// @return head の直前の文字が'\\n'であれば true
static bool is_at_line_start(const std::string& text, size_t head) {
	// 直前が \n の場合
	if (head > 0) {
		if (text[head - 1] == '\n') {
			return true;
		}
	}
	return false;
}


/// [Object.x] を展開し、ObjSec ベクターに格納
/// @param alias エイリアスデータ
/// @return 解析済みの ObjSec ベクター
std::vector<ObjSec> parse_objects(const std::string& alias) {
	std::vector<ObjSec> out;

	// [Object.0]以降を探すように初期化
	size_t start_pos = alias.find("[Object.0]");
	if (start_pos == std::string::npos) {
		return out;
	}

	const std::string& text_to_parse = alias;
	size_t pos = start_pos;

	// --- [Object.x] セクションの繰り返し処理 ---
	while (pos < text_to_parse.size()) {
		// 現在位置から次の [Object. を探す
		size_t head = text_to_parse.find("[Object.", pos);
		if (head == std::string::npos) break;

		// 行頭の [Object.x] でない場合は無視
		if (!is_at_line_start(text_to_parse, head)) {
			pos = head + 1;
			continue;
		}

		// セクションヘッダーの ] を探す
		size_t bracket_end = text_to_parse.find("]", head);
		if (bracket_end == std::string::npos) break;

		// 現在セクションの次の [Object. を見つける
		size_t next_head = std::string::npos;
		size_t search_pos = bracket_end + 1;
		while (search_pos < text_to_parse.size()) {
			size_t potential_next = text_to_parse.find("[Object.", search_pos);
			if (potential_next == std::string::npos) break;

			// 行頭チェック
			if (is_at_line_start(text_to_parse, potential_next)) {
				next_head = potential_next;
				break;
			}
			search_pos = potential_next + 1;
		}

		// 次の [Object. が見つからなかった場合、文字列の末尾まで
		if (next_head == std::string::npos) {
			next_head = text_to_parse.size();
		}


		// セクションごとにObjSecへパースする
		std::string sec = text_to_parse.substr(head, next_head - head);

		// インデックス番号を抽出
		const int prefix_len = 8;
		std::string num_str = text_to_parse.substr(head + prefix_len, bracket_end - (head + prefix_len));
		int idx = std::stoi(num_str);

		// effect.name の値を抽出
		std::string effect_name;
		const char* effect_key = "effect.name=";
		size_t efp = sec.find(effect_key);
		if (efp != std::string::npos) {
			efp += std::strlen(effect_key);
			size_t eol = sec.find('\r', efp);
			if (eol != std::string::npos) {
				effect_name = sec.substr(efp, eol - efp);
			}
		}

		// 抽出した情報を結果ベクタに追加し、次の検索位置を更新する
		out.push_back({ sec, idx, effect_name });
		pos = next_head;
	}

	return out;
}


/// フィルタ効果の開始インデックスを計算
/// @param objs 解析済みの ObjSec ベクター
/// @param include_self_filter 自身のフィルタ効果を対象にするか
/// @return 2 または 1
int calc_start_index(const std::vector<ObjSec>& objs, bool include_self_filter) {
	if (include_self_filter) {
		if (has_output_section(objs)) {
			// 出力切り替えセクションがある場合、フィルタ効果は[Object.2]以降
			return 2;
		}
		else if (is_none_output_object(objs)){
			// 特殊メディアオブジェクトかフィルタオブジェクトの場合、フィルタ効果は[Object.1]以降
			return 1;
		}
		else {
			// それ以外(フィルタ効果)は[Object.0]以降
			return 0;
		}
	}
	else {
		// フィルタオブジェクトなら、追加フィルタ効果は[Object.2]以降
		if (objs[0].effect_name == u8"フィルタオブジェクト") return 2;

		// 自身のフィルタ効果を対象にしない場合、追加フィルタ効果は[Object.1] (+出力切り替えセクション) 以降
		return 1 + has_output_section(objs);
	}
}


/// 出力切り替えセクションがあるかを判定
/// @param objs 解析済みの ObjSec ベクター
/// @return true/false
bool has_output_section(const std::vector<ObjSec>& objs) {
	for (auto& s : OUTPUT_SECTION_LIST) {
		if (objs[1].effect_name == s) return true;
	}
	return false;
}


/// 特殊メディアオブジェクトか判定 (グループ制御や部分フィルタなど、出力切り替えセクションを持たないもの)
/// @param objs 解析済みの ObjSec ベクター
/// @return true/false
bool is_none_output_object(const std::vector<ObjSec>& objs) {
	for (auto& s : NON_OUTPUT_SECTION_OBJECT_LIST) {
		if (objs[0].effect_name == s) return true;
	}
	return false;
}


/// ObjSec からエイリアスデータを再構築する
/// @param objs 処理対象の ObjSec
/// @param start_index 再構築処理を開始する ObjSec のインデックス
/// @param base_index 新しい [Object.x] の基点
/// @return 再構築されたエイリアスデータ
std::string rebuild_alias(
	const std::vector<ObjSec>& objs,
	int start_index,
	int base_index
) {
	if (start_index >= objs.size()) return "";

	std::string result = "";

	int new_idx = base_index;

	for (int i = start_index; i < objs.size(); i++) {
		char new_head[32];
		sprintf_s(new_head, "[Object.%d]", new_idx);
		new_idx++;

		std::string sec = objs[i].sec;
		size_t old_end = sec.find("]");
		result += std::string(new_head) + sec.substr(old_end + 1);
	}
	return result;
}


/// エイリアスに付くフィルタを抽出して、フィルタ効果オブジェクトを作成
/// @param alias: エイリアスデータ
/// @return フィルタ効果オブジェクトのエイリアスデータ
std::string build_target_alias(const std::string alias) {
	std::string header = extract_object_header(alias);
	const auto objs = parse_objects(alias);
	if (objs.empty()) return "";

	int start = calc_start_index(objs);

	// 再構築 [Object]～[Object.0]～[Object.n]
	if (objs[0].effect_name == "フィルタオブジェクト") {
		return header + FILTER_OBJECT_OBJ0 + rebuild_alias(objs, start, 1);
	}
	else {
		return header + rebuild_alias(objs, start, 0);
	}
}


/// エイリアスに付くフィルタを抽出して、フィルタ効果群を作成（グループ制御に紐づける用）
/// @param alias: エイリアスデータ
/// @return フィルタ効果オブジェクト群を含むエイリアス文字列。
static std::string build_target_alias_group(const std::string alias) {
	auto objs = parse_objects(alias);
	if (objs.empty()) return "";

	int start = calc_start_index(objs);

	// 再構築 [Object.1]～[Object.n]
	return rebuild_alias(objs, start, 1);
}


/// エイリアスに付くフィルタを抽出して、グループ制御オブジェクトを作成
/// @param edit: 編集セクション構造体
/// @param alias: エイリアスデータ
/// @return グループ制御オブジェクトのハンドル (作成できなければ nullptr)
OBJECT_HANDLE try_create_group(
	EDIT_SECTION* edit,
	const std::string& alias,
	int layer, int start, int length)
{
	// [Object] ヘッダを抽出
	std::string header = extract_object_header(alias);

	// [Object.1]～ を作成
	std::string obj1 = build_target_alias_group(alias);

	// グループ制御を作成
	{
		std::string a = header + std::string(GROUP_OBJ0) + obj1;
		auto o = edit->create_object_from_alias(a.c_str(), layer, start, length);
		if (o) return o;
	}

	// 上記がdifferent effect typeで作成できなかったら、グループ制御(音声) を作る
	{
		std::string a = header + std::string(GROUP_AUDIO_OBJ0) + obj1;
		auto o = edit->create_object_from_alias(a.c_str(), layer, start, length);
		if (o) return o;
	}

	return nullptr;
}


/// 元オブジェクトから分離フィルタを削除したものを作成
/// @param alias: エイリアスデータ
std::string build_source_alias(const std::string alias) {
	// エイリアスデータをパース
	const auto objs = parse_objects(alias);
	if (objs.empty()) return "";

	// フィルタ効果の開始地点までを対象
	const int max_idx = calc_start_index(objs);

	// オブジェクトの再構築
	std::string header = extract_object_header(alias);

	// [Object.0] ～ [Object.0 or 1] までを切り出す
	std::vector<ObjSec> filtered_objs;
	filtered_objs.assign(objs.begin(), objs.begin() + max_idx);

	return header + rebuild_alias(filtered_objs, 0, 0);
}


/// 指定範囲に被らない最初のレイヤーを返す
/// @param edit 編集セクションハンドル
/// @param start_layer 探索を開始するレイヤー（通常は元レイヤー+1）
/// @param start_frame 探索対象の開始フレーム
/// @param end_frame 探索対象の終了フレーム（開始 + 長さ）
int find_available_layer(EDIT_SECTION* edit, int start_layer, int start_frame, int end_frame) {
	for (int layer = start_layer; layer < SAFE_LAYER_LIMIT; ++layer) {
		auto obj = edit->find_object(layer, start_frame);
		if (!obj) {
			// 指定フレームにオブジェクトが無ければそのレイヤーは使える
			return layer;
		}
		auto lf = edit->get_object_layer_frame(obj);
		// 重複判定: 範囲が交差するなら不可
		// 交差しない条件: (lf.end <= start_frame) || (lf.start >= end_frame)
		if (lf.end < start_frame || lf.start > end_frame) {
			return layer;
		}
		// 交差する場合は次のレイヤーを試す
	}
	return -1;
}