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

        // ObjSec を作成
        ObjSec o;
        o.sec = sec;
        o.index = idx;
        o.effect_name = effect_name;

		// --- パラメータ抽出の繰り返し処理 ---
		size_t cur = 0;
		while (cur < sec.size()) {
			// 現在位置から行末の CRLF を探す
			size_t line_end = sec.find("\r\n", cur);
			std::string line;

			if (line_end == std::string::npos) {
				// 次の CRLF が見つからなかった場合、行の末尾まで
				line = sec.substr(cur);
				cur = sec.size();
			}
			else {
				// CRLF が見つかった場合、CRLF の直前まで
				line = sec.substr(cur, line_end - cur);
				cur = line_end + 2;
			}

			// セクションヘッダー行は無視
			if (line[0] == '[') continue;

			// key=value のパラメータを抽出
			size_t eq = line.find('=');
			std::string key = line.substr(0, eq);
			std::string value = line.substr(eq + 1);

			// props に保存する
			if (!key.empty()) {
				o.props[key] = value;
			}
		}

		// 抽出した情報を結果ベクタに追加し、次の検索位置を更新する
		out.push_back(o);
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


/// エイリアスに付くフィルタを抽出して、フィルタ効果オブジェクトを作成する
/// @param alias エイリアスデータ
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


/// エイリアスに付くフィルタを抽出して、フィルタ効果群を作成する
/// @param alias エイリアスデータ
/// @param index セクションの構築開始位置
/// @return フィルタ効果オブジェクト群を含むエイリアス文字列
static std::string build_target_alias_group(const std::string alias, int index) {
	auto objs = parse_objects(alias);
	if (objs.empty()) return "";

	int start = calc_start_index(objs);

	// 再構築 [Object.index]～[Object.n]
	return rebuild_alias(objs, start, index);
}


/// エイリアスに付くフィルタを抽出して、グループ制御オブジェクトを作成する
/// @param edit 編集セクション構造体
/// @param alias エイリアスデータ
/// @param layer オブジェクトを作成するレイヤー番号
/// @param start オブジェクトの開始フレーム
/// @param length オブジェクトの長さ
/// @return グループ制御オブジェクトのハンドル。作成できなければ nullptr
OBJECT_HANDLE try_create_group(
	EDIT_SECTION* edit,
	const std::string& alias,
	int layer, int start, int length)
{
	// [Object] ヘッダを抽出
	std::string header = extract_object_header(alias);

	// [Object.1]～ を作成
	std::string obj1 = build_target_alias_group(alias, 1);

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


/// エイリアスに付くフィルタを抽出して、画像合成オブジェクトを作成する
/// @param edit 編集セクション構造体
/// @param alias エイリアスデータ
/// @param layer オブジェクトを作成するレイヤー番号
/// @param start オブジェクトの開始フレーム
/// @param length オブジェクトの長さ
/// @return 画像合成オブジェクトのハンドル (作成できなければ nullptr)
OBJECT_HANDLE try_create_composite(
	EDIT_SECTION* edit,
	const std::string& alias,
	int layer, int start, int length)
{
	// [Object] ヘッダを抽出
	std::string header = extract_object_header(alias);

	// [Object.2]～ を作成
	std::string obj2 = build_target_alias_group(alias, 2);

	// 画像合成を作成
	{
		std::string a = header + std::string(COMPOSITE_OBJ01) + obj2;
		auto o = edit->create_object_from_alias(a.c_str(), layer, start, length);
		if (o) return o;
	}

	return nullptr;
}


/// 元オブジェクトから分離フィルタを削除したエイリアスを作成する
/// @param alias エイリアスデータ
/// @return 分離フィルタを除いた元オブジェクトのエイリアスデータ
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
/// @return 使用可能なレイヤー番号。見つからなければ -1
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


/// 標準描画の「拡大率」と「縦横比」を拡大率フィルタに変換する
/// @param src 変換元のオブジェクトセクション
/// @param def 基本効果フィルタ定義
/// @param out 生成結果のオブジェクトセクション
/// @return 拡大率フィルタを生成した場合は true
static bool build_zoom_filter(const ObjSec& src, const BasicEffectDef& def, ObjSec& out) {
	// 拡大率
	double scale = 100.000;
	auto scale_it = src.props.find(u8"拡大率");
	bool has_scale_prop = scale_it != src.props.end();
	bool has_scale = has_scale_prop && get_prop_as_double(src, u8"拡大率", scale);
	// 移動無しかつ、拡大率 = 100.000 の場合は、フィルタの作成対象外
	bool scale_no_change = !has_scale_prop || (has_scale && scale == 100.000);

	// 縦横比
	double aspect = 0.000;
	auto aspect_it = src.props.find(u8"縦横比");
	bool has_aspect_prop = aspect_it != src.props.end();
	bool has_aspect = has_aspect_prop && get_prop_as_double(src, u8"縦横比", aspect);
	// 移動ありもしくは、縦横比 = 0.000 の場合は、フィルタの作成対象外
	bool aspect_no_change = !has_aspect_prop || !has_aspect || aspect == 0.000;
	
	if (scale_no_change && aspect_no_change) return false;

	// 拡大率の値を取得
	std::string scale_val = (!has_scale_prop || scale_no_change) ? "100.000" : scale_it->second;
	
	// 縦横比からX拡大率 / Y拡大率を算出
	std::string x_val = "100.000";
	std::string y_val = "100.000";

	if (!aspect_no_change) {
		if (aspect < 0) {
			char buf[64];
			sprintf_s(buf, "%.3f", 100.0 + aspect);
			y_val = buf;
		}
		else if (aspect > 0) {
			char buf[64];
			sprintf_s(buf, "%.3f", 100.0 - aspect);
			x_val = buf;
		}
	}

	out.props[u8"拡大率"] = scale_val;
	out.props["X"] = x_val;
	out.props["Y"] = y_val;
	out.props["Z"] = "100.000";

	out.sec = std::string("[Object.0]\r\n")
		+ u8"effect.name=拡大率\r\n"
		+ u8"拡大率=" + scale_val + "\r\n"
		+ "X=" + x_val + "\r\n"
		+ "Y=" + y_val + "\r\n"
		+ "Z=100.000\r\n";

	return true;
}


/// 拡大率の元パラメータをリセットする
/// @param src リセット対象のオブジェクトセクション
/// @param def 基本効果フィルタ定義
static void reset_zoom_params(ObjSec& src, const BasicEffectDef& def) {
	src.props[u8"拡大率"] = "100.000";
	double aspect = 0.000;
	if (get_prop_as_double(src, u8"縦横比", aspect)) {
		// 移動なしの場合はリセット
		src.props[u8"縦横比"] = "0.000";
	}
}


/// 基本効果フィルタ定義テーブル
const std::vector<BasicEffectDef> BASIC_EFFECT_DEFS = {
	{ u8"座標", {						// 転記先フィルタ名
		{ "X",			 "X", "0.00" },	// 標準描画での転記元パラメータ名, 転記先パラメータ名, デフォルト値
		{ "Y",			 "Y", "0.00" },
		{ "Z",			 "Z", "0.00" },
	}, nullptr, nullptr},				// (転記以外のときの)フィルタビルド関数、元パラメータリセット関数
	{ u8"回転中心@Basic_S", {
		{ u8"中心X",	 "X", "0.00" },
		{ u8"中心Y",	 "Y", "0.00" },
		{ u8"中心Z",	 "Z", "0.00" },
	}, nullptr, nullptr},
	{ u8"回転", {
		{ u8"X軸回転",	 "X", "0.00" },
		{ u8"Y軸回転",	 "Y", "0.00" },
		{ u8"Z軸回転",	 "Z", "0.00" },
	}, nullptr, nullptr},
	{ u8"拡大率", {
		{ u8"拡大率",	 u8"拡大率", "100.000" },
		{ u8"縦横比",	 u8"縦横比", "0.000" },
	}, build_zoom_filter, reset_zoom_params },
	{ u8"透明度", {
		{ u8"透明度",	 u8"透明度", "0.00" },
	}, nullptr, nullptr},
};


/// 基本効果のフィルタ群を生成する
/// @param objs 解析済みの ObjSec ベクター
/// @return 生成した基本効果フィルタ群
std::vector<ObjSec> build_basic_effect_filters(const std::vector<ObjSec>& objs) {
	std::vector<ObjSec> results;

	if (objs.size() < 2) return results;
	if (!has_output_section(objs)) return results;

	for (auto& def : BASIC_EFFECT_DEFS) {
		ObjSec out;
		out.index = 0;
		out.effect_name = def.effect_name;

		if (def.custom_build) {
			// カスタムビルダーに委譲
			if (def.custom_build(objs[1], def, out)) {
				results.push_back(out);
			}
		}
		else {
			// 通常マッピング: 全マッピングがデフォルトならスキップ
			bool all_default = true;
			for (auto& m : def.mappings) {
				if (!is_prop_default(objs[1], m.src_key, m.default_val)) {
					all_default = false;
					break;
				}
			}
			if (all_default) continue;

			std::string body;
			for (auto& m : def.mappings) {
				bool is_default = is_prop_default(objs[1], m.src_key, m.default_val);
				std::string val = is_default ? m.default_val : objs[1].props.at(m.src_key);
				out.props[m.dst_key] = val;
				body += m.dst_key + "=" + val + "\r\n";
			}

			out.sec = std::string("[Object.0]\r\n")
				+ "effect.name=" + def.effect_name + "\r\n"
				+ body;
			results.push_back(out);
		}
	}

	return results;
}


/// 基本効果から分離された元パラメータをリセットする
/// @param objs リセット対象を含む ObjSec ベクター
/// @param filters 分離した基本効果フィルタ群
void reset_basic_effect_params(std::vector<ObjSec>& objs, const std::vector<ObjSec>& filters) {
	if (objs.size() < 2) return;

	// filters に含まれる effect_name に対応する定義を探してリセット
	for (auto& filter : filters) {
		for (auto& def : BASIC_EFFECT_DEFS) {
			if (def.effect_name != filter.effect_name) continue;
			if (def.custom_reset) {
				def.custom_reset(objs[1], def);
			}
			else {
				for (auto& m : def.mappings) {
					objs[1].props[m.src_key] = m.default_val;
				}
			}
			break;
		}
	}

	// sec 文字列を再構築
	size_t old_end = objs[1].sec.find("]");
	std::string header_part = objs[1].sec.substr(0, (old_end == std::string::npos) ? 0 : old_end + 1);
	std::string body;
	body += std::string("\r\n") + "effect.name=" + objs[1].effect_name + "\r\n";
	for (auto& kv : objs[1].props) {
		if (kv.first == "effect.name") continue;
		body += kv.first + "=" + kv.second + "\r\n";
	}
	if (!header_part.empty()) objs[1].sec = header_part + body;
}

/// out にパラメータの値を入れる ※移動ありの場合は取得できない
/// @param obj 参照元のオブジェクトセクション
/// @param key 取得するパラメータ名
/// @param out 取得した数値の出力先
/// @return 数値として取得できた場合は true
bool get_prop_as_double(const ObjSec& obj, const std::string& key, double& out) {
    auto it = obj.props.find(key);
    if (it == obj.props.end()) return false;	// key が存在しない場合は取得できない
    const std::string& val = it->second;
    if (val.find(',') != std::string::npos) return false;	// カンマを含む(移動あり)の場合は取得できない
    try {
        out = std::stod(val);
        return true;
    } catch (...) {
        return false;	// 数値でない場合(文字列)は取得できない
    }
}

/// props の値がデフォルト値かどうかを判定する
/// @param obj 判定対象のオブジェクトセクション
/// @param key 判定するパラメータ名
/// @param default_val 比較するデフォルト値
/// @return デフォルト値とみなせる場合は true
bool is_prop_default(const ObjSec& obj, const std::string& key, const std::string& default_val) {
	auto it = obj.props.find(key);
	// キーが存在しなければデフォルトとみなす
	if (it == obj.props.end()) return true;
	const std::string& val = it->second;
	// カンマを含む場合はアニメーションデータなので非デフォルト
	if (val.find(',') != std::string::npos) return false;

	try {
		double current = std::stod(val);
		double def = std::stod(default_val);
		return current == def;
	}
	catch (...) {
		// 数値でない場合は完全一致のみデフォルト
		return val == default_val;
	}
}
