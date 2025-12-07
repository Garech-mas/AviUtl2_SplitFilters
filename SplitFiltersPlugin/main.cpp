#include "main.h" 

// --- 外部変数の実体定義 ---
EDIT_HANDLE* edit_handle;
LOG_HANDLE* logger;


/// オブジェクトメニュー「フィルタ分離」
/// 選択中オブジェクトのフィルタ効果部をフィルタオブジェクトに分離する
static void __cdecl split_filters_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	// 選択中オブジェクトがない場合（あり得ないけど念のため）
	if (sel_num <= 0) {
		logger->error(logger, L"選択オブジェクトがありません。");
		return;
	}

	for (int i = 0; i < sel_num; i++) {
		OBJECT_HANDLE obj = edit->get_selected_object(i);

		auto lf = edit->get_object_layer_frame(obj);
		auto alias = edit->get_object_alias(obj);
		// get_object_aliasに失敗した場合（不正なオブジェクト？）
		if (!alias) {
			logger->error(logger, L"エイリアスデータの取得に失敗しました。");
			continue;
		}

		// === 解析開始 ===
		std::string a(alias);
		auto objs = parse_objects(a);

		// 追加フィルタ効果がない場合
		if (calc_start_index(objs) >= objs.size()) {
			logger->info(logger, L"抽出できるフィルタ効果がありません。");
			continue;
		}

		// フィルタ効果オブジェクト
		std::string target = build_target_alias(alias);
		if (target.empty()) {
			logger->error(logger, L"フィルタ効果オブジェクトの生成に失敗しました。");
			continue;
		}

		// 元オブジェクト - 分離フィルタ
		std::string new_src_alias = build_source_alias(alias);
		if (new_src_alias.empty()) {
			logger->warn(logger, L"元オブジェクトの生成に失敗しました。");
			continue;
		}

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
				logger->error(logger, L"元オブジェクトの再作成に失敗しました。");
				MessageBox(get_aviutl2_window(), L"元オブジェクトの再作成に失敗しました。", PLUGIN_TITLE, MB_ICONWARNING);
				continue;
			}
		}

		// === 複製先フィルタの追加 ===
		bool created = false;

		for (int layer = lf.layer + 1; layer < SAFE_LAYER_LIMIT; layer++) {
			auto new_obj = edit->create_object_from_alias(
				target.c_str(),
				layer,
				lf.start,
				lf.end - lf.start
			);
			if (new_obj) {
				edit->set_focus_object(new_obj);
				created = true;
				break;
			}
		}

		if (!created) {
			logger->error(logger, L"フィルタ効果オブジェクトの作成に失敗しました。");
			MessageBox(get_aviutl2_window(), L"フィルタ効果オブジェクトの作成に失敗しました。", PLUGIN_TITLE, MB_ICONWARNING);
		}


	}
}


/// オブジェクトメニュー「フィルタ分離（グループ制御）」
/// 選択中オブジェクトのフィルタ効果部をグループ制御オブジェクトに分離する
static void __cdecl split_filters_for_group_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	// 選択中オブジェクトがない場合（あり得ないけど念のため）
	if (sel_num <= 0) {
		logger->error(logger, L"選択オブジェクトがありません。");
		return;
	}

	for (int i = 0; i < sel_num; i++) {
		OBJECT_HANDLE obj = edit->get_selected_object(i);

		auto lf = edit->get_object_layer_frame(obj);
		auto alias = edit->get_object_alias(obj);
		// get_object_aliasに失敗した場合（不正なオブジェクト？）
		if (!alias) {
			logger->error(logger, L"エイリアスデータの取得に失敗しました。");
			continue;
		}

		// === 解析開始 ===
		std::string a(alias);
		auto objs = parse_objects(a);

		// 追加フィルタ効果がない場合
		if (calc_start_index(objs) >= objs.size()) {
			logger->info(logger, L"抽出できるフィルタ効果がありません。");
			continue;
		}

		// 元オブジェクト - 分離フィルタ
		std::string new_src_alias = build_source_alias(alias);
		if (new_src_alias.empty()) {
			logger->warn(logger, L"元オブジェクトの生成に失敗しました。");
			continue;
		}

		// === 元オブジェクトの置き換え (1レイヤー下に置く) ===
		edit->delete_object(obj);
		bool created = false;

		for (int layer = lf.layer + 1; layer < SAFE_LAYER_LIMIT; layer++) {

			auto new_obj = edit->create_object_from_alias(
				new_src_alias.c_str(),
				layer,
				lf.start,
				lf.end - lf.start
			);
			if (new_obj) {
				created = true;
				break;
			}
		}

		if (!created) {
			logger->error(logger, L"元オブジェクトの再作成に失敗しました。");
			MessageBox(get_aviutl2_window(), L"元オブジェクトの再作成に失敗しました。", PLUGIN_TITLE, MB_ICONWARNING);
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
			logger->error(logger, L"グループ制御オブジェクトの作成に失敗しました。");
			MessageBox(get_aviutl2_window(), L"グループ制御オブジェクトの作成に失敗しました。", PLUGIN_TITLE, MB_ICONWARNING);
			continue;
		}

		edit->set_focus_object(group_obj);

	}
}


/// オブジェクトメニュー「フィルタ結合」
/// 選択中オブジェクトを上レイヤーのオブジェクトに結合する
/// ※選択中オブジェクトが出力切替オブジェクト持ちなら、その下のオブジェクトを選択中オブジェクトに結合する
static void __cdecl merge_filters_callback(EDIT_SECTION* edit) {
	int sel_num = edit->get_selected_object_num();
	if (sel_num <= 0) {
		logger->error(logger, L"選択オブジェクトがありません。");
		return;
	}

	for (int i = 0; i < sel_num; i++) {
		OBJECT_HANDLE selected_obj = edit->get_selected_object(i);
		auto selected_lf = edit->get_object_layer_frame(selected_obj);
		const char* selected_alias_str = edit->get_object_alias(selected_obj);
		if (!selected_alias_str) {
			logger->error(logger, L"選択されたオブジェクトのエイリアス取得に失敗しました。");
			continue;
		}

		auto selected_objs = parse_objects(selected_alias_str);

		// --- 結合対象のオブジェクトを探索 ---

		auto filter_obj = selected_obj;
		auto filter_lf = selected_lf;

		// source_objを探す
		auto source_obj = edit->find_object(filter_lf.layer - 1, filter_lf.start);
		if (filter_lf.layer - 1 < 0) {
			logger->error(logger, L"上のオブジェクトが見つかりませんでした。" PLUGIN_TITLE);
			return;
		} else if (!source_obj) {
			continue;
		}

		auto source_lf = edit->get_object_layer_frame(source_obj);
		if (filter_lf.end >= source_lf.start) {
			break;
		}

		auto filter_objs = parse_objects(edit->get_object_alias(filter_obj));
		auto source_alias = edit->get_object_alias(source_obj);
		auto source_objs = parse_objects(source_alias);

		// filter_obj -> source_objに結合する
		source_objs.insert(source_objs.end(), filter_objs.begin(), filter_objs.end());

		auto merged_alias = extract_object_header(source_alias) + rebuild_alias(source_objs, 0, 0);

		// 削除・配置
		edit->delete_object(source_obj);
		edit->delete_object(filter_obj);

		auto merged_obj = edit->create_object_from_alias(
			merged_alias.c_str(),
			source_lf.layer,
			source_lf.start,
			source_lf.end - source_lf.start
		);
		if (!merged_obj) {
			logger->error(logger, L"元オブジェクトの再作成に失敗しました。" PLUGIN_TITLE);
			MessageBox(get_aviutl2_window(), L"元オブジェクトの再作成に失敗しました。", PLUGIN_TITLE, MB_ICONWARNING);
			continue;
		}
		edit->set_focus_object(merged_obj);
	}
}


///	ログ出力機能初期化
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
	logger = handle;
}


/// プラグイン登録
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	host->set_plugin_information(PLUGIN_INFO);
	host->register_object_menu(L"フィルタ分離", split_filters_callback);
	host->register_object_menu(L"フィルタ分離（グループ制御）", split_filters_for_group_callback);
	host->register_object_menu(L"上のオブジェクトへフィルタ結合", merge_filters_callback);
	edit_handle = host->create_edit_handle();
}