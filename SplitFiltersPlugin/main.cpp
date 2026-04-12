#include "main.h"

// --- 外部変数の実体定義 ---
EDIT_HANDLE* edit_handle;
LOG_HANDLE* logger;
CONFIG_HANDLE* config;

static std::wstring Plugin_Name;
static std::wstring Plugin_Title;
static std::wstring Plugin_Info;

static std::vector<std::wstring> g_registered_menu_names;

/// 回転中心@Basic_S スクリプトが導入されているか
static bool g_has_basic_s = false;

/// 選択オブジェクトまたはフォーカスオブジェクトを取得する
static OBJECT_HANDLE get_target_object(EDIT_SECTION* edit, int index) {
	OBJECT_HANDLE obj = edit->get_selected_object(index);
	if (!obj) {
		obj = edit->get_focus_object();
		if (!obj) {
			logger->info(logger, config->translate(config, L"選択オブジェクトがありません。"));
			MessageBeep(-1);
		}
	}
	return obj;
}

/// 上レイヤーにある重複オブジェクトを探す
static OBJECT_HANDLE find_object_above(
	EDIT_SECTION* edit,
	const OBJECT_LAYER_FRAME& lf,
	OBJECT_LAYER_FRAME& out_lf)
{
	for (int j = 1; j < SAFE_LAYER_LIMIT; j++) {
		if (lf.layer - j < 0) break;
		OBJECT_HANDLE obj = edit->find_object(lf.layer - j, lf.start);
		if (!obj) continue;
		out_lf = edit->get_object_layer_frame(obj);
		if (lf.end >= out_lf.start) return obj;
	}
	logger->info(logger, config->translate(config, L"上のオブジェクトが存在しません。"));
	MessageBeep(-1);
	return nullptr;
}

/// 結合失敗時に元オブジェクトの復旧を試みる
/// @return 復旧に成功した場合は true
static bool try_recover_objects(
	EDIT_SECTION* edit,
	const std::string& source_alias,
	const OBJECT_LAYER_FRAME& source_lf,
	const std::string& selected_alias,
	const OBJECT_LAYER_FRAME& selected_lf,
	const std::string& merged_alias_str)
{
	MessageBeep(-1);
	auto chk1 = edit->create_object_from_alias(
		source_alias.c_str(),
		source_lf.layer,
		source_lf.start,
		source_lf.end - source_lf.start
	);
	auto chk2 = edit->create_object_from_alias(
		selected_alias.c_str(),
		selected_lf.layer,
		selected_lf.start,
		selected_lf.end - selected_lf.start
	);

	if (chk1 && chk2) {
		edit->set_focus_object(chk2);
		logger->warn(logger, config->translate(config, L"フィルタ結合に失敗しました。元オブジェクトを復旧しました。"));
	}
	else {
		logger->warn(logger, config->translate(config, L"元オブジェクトの作成に失敗しました。"));
	}
	std::wstring merged_alias_w = utf8_to_wide(merged_alias_str);
	logger->verbose(logger, merged_alias_w.c_str());
	return (chk1 && chk2);
}

// ---------------------------------------------------------------------------
// コールバック関数
// ---------------------------------------------------------------------------

/// オブジェクトメニュー「フィルタ分離」
/// 選択中オブジェクトのフィルタ効果部をフィルタオブジェクトに分離する
static void __cdecl split_filters_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	int i = 0;
	do {
		OBJECT_HANDLE obj = get_target_object(edit, i++);
		if (!obj) return;

		auto lf = edit->get_object_layer_frame(obj);
		auto alias = edit->get_object_alias(obj);

		// === 解析開始 ===
		std::string a(alias);
		auto objs = parse_objects(a);

		// 追加フィルタ効果がない場合
		if (calc_start_index(objs) >= objs.size()) {
			logger->info(logger, config->translate(config, L"抽出できるフィルタ効果がありません。"));
			MessageBeep(-1);
			continue;
		}

		// フィルタ効果オブジェクト
		std::string target = build_target_alias(alias);

		// 元オブジェクト - 分離フィルタ
		std::string new_src_alias = build_source_alias(alias);

		// === 元オブジェクトの置き換え ===
		edit->delete_object(obj);
		{
			auto new_obj0 = edit->create_object_from_alias(
				new_src_alias.c_str(),
				lf.layer,
				lf.start,
				lf.end - lf.start
			);
			if (!new_obj0) {
				logger->warn(logger, config->translate(config, L"元オブジェクトの作成に失敗しました。"));
				continue;
			}
		}

		// === 複製先フィルタの追加 ===
		bool created = false;

		// 重複しない最初のレイヤーを探して作成する
		int free_layer = find_available_layer(edit, lf.layer + 1, lf.start, lf.end);
		if (free_layer != -1) {
			auto new_obj = edit->create_object_from_alias(
				target.c_str(),
				free_layer,
				lf.start,
				lf.end - lf.start
			);
			if (new_obj) {
				edit->set_object_name(new_obj, nullptr);
				edit->set_focus_object(new_obj);
				created = true;
			}
		}

		if (!created) {
			logger->warn(logger, config->translate(config, L"フィルタ効果オブジェクトの作成に失敗しました。"));
		}
	} while (i < sel_num);
}


/// オブジェクトメニュー「フィルタ分離（グループ制御）」
/// 選択中オブジェクトのフィルタ効果部をグループ制御オブジェクトに分離する
static void __cdecl split_filters_for_group_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	int i = 0;
	do {
		OBJECT_HANDLE obj = get_target_object(edit, i++);
		if (!obj) return;

		auto lf = edit->get_object_layer_frame(obj);
		auto alias = edit->get_object_alias(obj);

		// === 解析開始 ===
		std::string a(alias);
		auto objs = parse_objects(a);

		// 追加フィルタ効果がない場合
		if (calc_start_index(objs) >= objs.size()) {
			logger->info(logger, config->translate(config, L"抽出できるフィルタ効果がありません。"));
			MessageBeep(-1);
			continue;
		}

		// 元オブジェクト - 分離フィルタ
		std::string new_src_alias = build_source_alias(alias);

		// === 元オブジェクトの置き換え (1レイヤー下に置く) ===
		edit->delete_object(obj);
		bool created = false;

		// 重複しないレイヤーを探して元オブジェクトを作成
		int free_layer = find_available_layer(edit, lf.layer + 1, lf.start, lf.end);
		if (free_layer != -1) {
			auto new_obj = edit->create_object_from_alias(
				new_src_alias.c_str(),
				free_layer,
				lf.start,
				lf.end - lf.start
			);
			if (new_obj) {
				created = true;
			}
		}

		if (!created) {
			logger->warn(logger, config->translate(config, L"元オブジェクトの作成に失敗しました。"));
			continue;
		}

		// 選択レイヤーにグループ制御を追加
		auto group_obj = try_create_group(
			edit,
			std::string(alias),
			lf.layer, lf.start,
			lf.end - lf.start
		);

		if (!group_obj) {
			logger->warn(logger, config->translate(config, L"グループ制御オブジェクトの作成に失敗しました。"));
			continue;
		}

		edit->set_object_name(group_obj, nullptr);
		edit->set_focus_object(group_obj);
	} while (i < sel_num);
}


/// オブジェクトメニュー「フィルタ分離（画像合成）」
/// 選択中オブジェクトのフィルタ効果部を画像合成オブジェクトに分離する
static void __cdecl split_filters_for_composite_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	int i = 0;
	do {
		OBJECT_HANDLE obj = get_target_object(edit, i++);
		if (!obj) return;

		auto lf = edit->get_object_layer_frame(obj);
		auto alias = edit->get_object_alias(obj);

		// === 解析開始 ===
		std::string a(alias);
		auto objs = parse_objects(a);

		// 追加フィルタ効果がない場合
		if (calc_start_index(objs) >= objs.size()) {
			logger->info(logger, config->translate(config, L"抽出できるフィルタ効果がありません。"));
			MessageBeep(-1);
			continue;
		}

		// 元オブジェクト - 分離フィルタ
		std::string new_src_alias = build_source_alias(alias);

		// === 元オブジェクトの置き換え (1レイヤー下に置く) ===
		edit->delete_object(obj);
		bool created = false;

		// 重複しないレイヤーを探して元オブジェクトを作成
		int free_layer = find_available_layer(edit, lf.layer + 1, lf.start, lf.end);
		if (free_layer != -1) {
			auto new_obj = edit->create_object_from_alias(
				new_src_alias.c_str(),
				free_layer,
				lf.start,
				lf.end - lf.start
			);
			if (new_obj) {
				created = true;
			}
		}

		if (!created) {
			logger->warn(logger, config->translate(config, L"元オブジェクトの作成に失敗しました。"));
			continue;
		}

		// 選択レイヤーに画像合成を追加
		auto group_obj = try_create_composite(
			edit,
			std::string(alias),
			lf.layer, lf.start,
			lf.end - lf.start
		);

		if (!group_obj) {
			logger->warn(logger, config->translate(config, L"画像合成オブジェクトの作成に失敗しました。"));
			continue;
		}

		edit->set_object_name(group_obj, nullptr);
		edit->set_focus_object(group_obj);
	} while (i < sel_num);
}


/// オブジェクトメニュー「基本効果分離」
/// 選択中オブジェクトの座標・中心座標・回転・拡大率・縦横比・透明度を基本効果フィルタに分離する
/// ※デフォルト値の場合は分離しない
static void __cdecl split_basic_effect_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	int i = 0;
	do {
		OBJECT_HANDLE obj = get_target_object(edit, i++);
		if (!obj) return;

		auto lf = edit->get_object_layer_frame(obj);
		auto alias = edit->get_object_alias(obj);

		// === 解析開始 ===
		std::string a(alias);
		auto objs = parse_objects(a);

		// テーブル定義に基づいて基本効果フィルタを生成
		auto filters = build_basic_effect_filters(objs);

		// Basic_S未導入の場合は、回転中心@Basic_Sを除外する
		if (!g_has_basic_s) {
			filters.erase(
				std::remove_if(filters.begin(), filters.end(), [](const ObjSec& obj) {
					return obj.effect_name == u8"回転中心@Basic_S";
					}),
				filters.end()
			);
		}

		if (filters.empty()) {
			logger->info(logger, config->translate(config, L"分離する基本効果がありません。"));
			MessageBeep(-1);
			continue;
		}

		// フィルタを分離した後の元オブジェクトのパラメータをリセット
		reset_basic_effect_params(objs, filters);

		// フィルタを標準描画の直下に挿入
		int insert_idx = calc_start_index(objs);
		std::vector<ObjSec> new_objs = objs;
		for (auto& f : filters) {
			new_objs.insert(new_objs.begin() + insert_idx, f);
			insert_idx++;
		}

		std::string new_alias = extract_object_header(a) + rebuild_alias(new_objs, 0, 0);
		edit->delete_object(obj);
		auto new_obj = edit->create_object_from_alias(
			new_alias.c_str(),
			lf.layer,
			lf.start,
			lf.end - lf.start
		);
		if (!new_obj) {
			logger->warn(logger, config->translate(config, L"基本効果フィルタの作成に失敗しました。"));
			continue;
		}
		edit->set_focus_object(new_obj);
	} while (i < sel_num);
}


/// オブジェクトメニュー「フィルタ結合」
/// 選択中オブジェクトを上レイヤーのオブジェクトに結合する
static void __cdecl merge_filters_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	int i = 0;
	do {
		OBJECT_HANDLE selected_obj = get_target_object(edit, i++);
		if (!selected_obj) return;

		auto selected_lf = edit->get_object_layer_frame(selected_obj);
		const char* selected_alias_c = edit->get_object_alias(selected_obj);
		std::string selected_alias = selected_alias_c ? selected_alias_c : std::string();
		auto selected_objs = parse_objects(selected_alias);

		// 追加フィルタ効果がない場合
		auto filter_start_idx = calc_start_index(selected_objs, true);
		if (filter_start_idx >= selected_objs.size()) {
			logger->info(logger, config->translate(config, L"抽出できるフィルタ効果がありません。"));
			MessageBeep(-1);
			continue;
		}

		// 上レイヤーのオブジェクトを探す
		OBJECT_LAYER_FRAME source_lf = {};
		OBJECT_HANDLE source_obj = find_object_above(edit, selected_lf, source_lf);
		if (!source_obj) continue;

		const char* source_alias_c = edit->get_object_alias(source_obj);
		std::string source_alias = source_alias_c ? source_alias_c : std::string();
		auto source_objs = parse_objects(source_alias);
		// selected_obj -> source_objに結合する
		source_objs.insert(source_objs.end(), selected_objs.begin() + filter_start_idx, selected_objs.end());
		auto merged_alias_str = extract_object_header(source_alias) + rebuild_alias(source_objs, 0, 0);

		// 削除・配置
		edit->delete_object(source_obj);
		edit->delete_object(selected_obj);
		auto merged_obj = edit->create_object_from_alias(
			merged_alias_str.c_str(),
			source_lf.layer,
			source_lf.start,
			source_lf.end - source_lf.start
		);
		if (!merged_obj) {
			try_recover_objects(edit, source_alias, source_lf, selected_alias, selected_lf, merged_alias_str);
			continue;
		}
		edit->set_focus_object(merged_obj);
	} while (i < sel_num);
}


/// オブジェクトメニュー「上のオブジェクトへ先頭フィルタを結合」
/// 選択中オブジェクトの"１番目のフィルタのみ"を上レイヤーのオブジェクトに結合する
static void __cdecl merge_head_filters_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	int i = 0;
	do {
		OBJECT_HANDLE selected_obj = get_target_object(edit, i++);
		if (!selected_obj) return;

		auto selected_lf = edit->get_object_layer_frame(selected_obj);
		const char* selected_alias_c = edit->get_object_alias(selected_obj);
		std::string selected_alias = selected_alias_c ? selected_alias_c : std::string();
		auto selected_objs = parse_objects(selected_alias);

		// 追加フィルタ効果がない場合
		auto filter_start_idx = calc_start_index(selected_objs, true);
		if (filter_start_idx >= selected_objs.size()) {
			logger->info(logger, config->translate(config, L"抽出できるフィルタ効果がありません。"));
			MessageBeep(-1);
			continue;
		}

		// 上レイヤーのオブジェクトを探す
		OBJECT_LAYER_FRAME source_lf = {};
		OBJECT_HANDLE source_obj = find_object_above(edit, selected_lf, source_lf);
		if (!source_obj) continue;

		const char* source_alias_c = edit->get_object_alias(source_obj);
		std::string source_alias = source_alias_c ? source_alias_c : std::string();
		auto source_objs = parse_objects(source_alias);

		// selected_obj の先頭にあるフィルタ１個を取り出す
		ObjSec moved_filter = selected_objs[filter_start_idx];
		selected_objs.erase(selected_objs.begin() + filter_start_idx);

		// moved_filter -> source_objに結合する
		source_objs.insert(source_objs.end(), moved_filter);
		auto merged_alias_str = extract_object_header(source_alias) + rebuild_alias(source_objs, 0, 0);

		// 新しい結合元オブジェクトのエイリアスを作成
		std::string new_selected_alias_str = extract_object_header(selected_alias) + rebuild_alias(selected_objs, 0, 0);

		// 結合対象のオブジェクトを削除・配置
		edit->delete_object(source_obj);
		auto merged_obj = edit->create_object_from_alias(
			merged_alias_str.c_str(),
			source_lf.layer,
			source_lf.start,
			source_lf.end - source_lf.start
		);

		if (!merged_obj) {
			try_recover_objects(edit, source_alias, source_lf, selected_alias, selected_lf, merged_alias_str);
			continue;
		}

		// 結合元のオブジェクトを削除・配置
		edit->delete_object(selected_obj);

		if (selected_objs.size() > filter_start_idx) {
			auto new_selected_obj = edit->create_object_from_alias(
				new_selected_alias_str.c_str(),
				selected_lf.layer,
				selected_lf.start,
				selected_lf.end - selected_lf.start
			);
			if (new_selected_obj) {
				edit->set_focus_object(new_selected_obj);
			}
			else
			{
				logger->warn(logger, config->translate(config, L"元オブジェクトの作成に失敗しました。"));
			}
		}
		else {
			edit->set_focus_object(merged_obj);
		}
	} while (i < sel_num);
}


/// プロジェクトを読み込んだときの処理
/// 回転中心@Basic_S スクリプトの有無を判定する
static void on_project_load(PROJECT_FILE* project) {
	if (!edit_handle) return;
	g_has_basic_s = false;

	edit_handle->enum_module_info(&g_has_basic_s,
		[](void* param, MODULE_INFO* info) {
			if (!param || !info || !info->name) return;
			if (info->type != MODULE_INFO::TYPE_SCRIPT_FILTER) return;

			if (wcsstr(info->name, L"回転中心@Basic_S") == nullptr &&
				(!info->information || wcsstr(info->information, L"回転中心@Basic_S") == nullptr)) {
				return;
			}

			*static_cast<bool*>(param) = true;
		}
	);
}


///	ログ出力機能初期化
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
	logger = handle;
}


///	設定関連初期化
EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
	config = handle;

	Plugin_Name = config->translate(config, PLUGIN_NAME);
	Plugin_Title = Plugin_Name + L" " + PLUGIN_VERSION;

	LPCWSTR info_fmt = config->translate(config, L"%ls %ls (テスト済: %ls) by Garech");
	wchar_t info_buf[512];
	std::swprintf(info_buf, 512, info_fmt, Plugin_Name.c_str(), PLUGIN_VERSION, TESTED_BETA);
	Plugin_Info = info_buf;
}


/// プラグインDLL初期化
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	if (version < TESTED_BETA_NO) {
		wchar_t msg[512];
		std::swprintf(msg, 512, config->translate(config, L"%lsを動作させるためには、AviUtl2 %lsが必要です。\nAviUtl2を更新してください。"), Plugin_Name.c_str(), TESTED_BETA);
		MessageBox(get_aviutl2_window(), msg, Plugin_Title.c_str(), MB_ICONWARNING);
		return false;
	}
	return true;
}


/// プラグイン登録
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	host->set_plugin_information(Plugin_Info.c_str());

	// 翻訳済のメニュー名を登録
	g_registered_menu_names.push_back(config->translate(config, L"フィルタ分離"));
	g_registered_menu_names.push_back(Plugin_Name + L"\\" + g_registered_menu_names.back());
	g_registered_menu_names.push_back(config->translate(config, L"フィルタ分離（グループ制御）"));
	g_registered_menu_names.push_back(Plugin_Name + L"\\" + g_registered_menu_names.back());
	g_registered_menu_names.push_back(config->translate(config, L"フィルタ分離（画像合成）"));
	g_registered_menu_names.push_back(Plugin_Name + L"\\" + g_registered_menu_names.back());
	g_registered_menu_names.push_back(config->translate(config, L"基本効果分離"));
	g_registered_menu_names.push_back(Plugin_Name + L"\\" + g_registered_menu_names.back());
	g_registered_menu_names.push_back(config->translate(config, L"上のオブジェクトへフィルタ結合"));
	g_registered_menu_names.push_back(Plugin_Name + L"\\" + g_registered_menu_names.back());
	g_registered_menu_names.push_back(config->translate(config, L"上のオブジェクトへ先頭フィルタを結合"));
	g_registered_menu_names.push_back(Plugin_Name + L"\\" + g_registered_menu_names.back());

	host->register_object_menu(g_registered_menu_names[0].c_str(), split_filters_callback);
	host->register_object_menu(g_registered_menu_names[2].c_str(), split_filters_for_group_callback);
	host->register_object_menu(g_registered_menu_names[4].c_str(), split_filters_for_composite_callback);
	host->register_object_menu(g_registered_menu_names[6].c_str(), split_basic_effect_callback);
	host->register_object_menu(g_registered_menu_names[8].c_str(), merge_filters_callback);
	host->register_object_menu(g_registered_menu_names[10].c_str(), merge_head_filters_callback);

	host->register_edit_menu(g_registered_menu_names[1].c_str(), split_filters_callback);
	host->register_edit_menu(g_registered_menu_names[3].c_str(), split_filters_for_group_callback);
	host->register_edit_menu(g_registered_menu_names[5].c_str(), split_filters_for_composite_callback);
	host->register_edit_menu(g_registered_menu_names[7].c_str(), split_basic_effect_callback);
	host->register_edit_menu(g_registered_menu_names[9].c_str(), merge_filters_callback);
	host->register_edit_menu(g_registered_menu_names[11].c_str(), merge_head_filters_callback);
	host->register_project_load_handler(on_project_load);

	edit_handle = host->create_edit_handle();
}