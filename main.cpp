#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>
#include "aviutl.hpp"
#include "exedit.hpp"
#include "detours.4.0.1/detours.h"
#pragma comment(lib, "detours.4.0.1/detours.lib")
#include "exin.hpp"

#undef min
#undef max

//
// プラグイン名です。
//
constexpr auto plugin_name = "継承元";

//
// 拡張編集にアクセスするためのオブジェクトです。
//
my::ExEditInternal exin;

//
// フック関連の処理です。
//
namespace hook
{
	//
	// 指定されたアドレスの関数をフックします。
	//
	template <typename ORIG_PROC, typename HOOK_PROC>
	BOOL attach(ORIG_PROC& orig_proc, const HOOK_PROC& hook_proc, uint32_t address)
	{
		auto exedit = (uint32_t)::GetModuleHandleA("exedit.auf");

		DetourTransactionBegin();
		DetourUpdateThread(::GetCurrentThread());

		orig_proc = (ORIG_PROC)(exedit + address);
		DetourAttach(&(PVOID&)orig_proc, hook_proc);

		DetourTransactionCommit();

		return TRUE;
	}

	//
	// 指定されたフックを解除します。
	//
	template <typename ORIG_PROC, typename HOOK_PROC>
	BOOL detach(ORIG_PROC& orig_proc, const HOOK_PROC& hook_proc)
	{
		DetourDetach(&(PVOID&)orig_proc, hook_proc);

		return TRUE;
	}
}

//
// 継承元を管理します。
//
namespace inheritance
{
	//
	// 継承元ノードです。
	//
	struct Node
	{
		ExEdit::Object* object;

		int32_t layer;
		int32_t frame_layer;
		BOOL same_group;
		BOOL no_inherit_draw_filter;

		int32_t layer_range;
	};

	//
	// 継承元ノードのコレクションです。
	//
	std::vector<std::shared_ptr<Node>> collection;

	//
	// レイヤー番号をキーにしたオブジェクトのマップです。
	//
	std::unordered_map<int32_t, ExEdit::Object*> object_map;

	//
	// コレクションをリセットします。
	//
	void reset()
	{
		collection.clear();
		object_map.clear();
	}

	//
	// 継承元ノードを追加します。
	//
	void add(ExEdit::Object* object, ExEdit::Filter* filter)
	{
		auto layer = filter->track[0];
		auto frame_layer = filter->track[1] - 1;
		auto same_group = filter->check[0];
		auto no_inherit_draw_filter = filter->check[1];

		auto layer_range = 100;
		if (layer) layer_range = object->layer_set + layer;

		collection.emplace_back(std::make_shared<Node>(
			object, layer, frame_layer, same_group, no_inherit_draw_filter, layer_range));
	}

	//
	// 処理中オブジェクトを対象としている継承元ノードを返します。
	//
	std::shared_ptr<Node> find(ExEdit::Object* processing_object, ExEdit::FilterProcInfo* efpip)
	{
		// 継承元ノードを走査します。
		auto c = collection.size();
		for (size_t i = 0; i < c; i++)
		{
			// 継承元ノードを逆順で取得します。
			auto back_index = c - i - 1;
			const auto& node = collection[back_index];

			// 処理中オブジェクトが継承元ノードの範囲内の場合は
			if (processing_object->layer_set <= node->layer_range)
			{
				// 継承元ノードが同じグループのオブジェクトのみを対象にしている場合は
				if (node->same_group)
				{
					// 処理中オブジェクトが同じグループかチェックします。
					if (processing_object->group_belong != node->object->group_belong)
						continue;
				}

				// この継承元ノードを返します。
				return node;
			}
			// 処理中オブジェクトが継承元ノードの範囲外の場合は
			else
			{
				// これ以降の処理中オブジェクトも必ずこの継承元ノードの範囲外になります。
				// よって、この継承元ノードはこれ以降使用されないのでコレクションから取り除きます。
				collection.erase(collection.begin() + back_index);
			}
		}

		// 継承元ノードが見つからなかったのでnullptrを返します。
		return nullptr;
	}

	//
	// 指定されたレイヤーにオブジェクトを関連付けます。
	//
	void add(int32_t layer, ExEdit::Object* object)
	{
		object_map[layer] = object;
	}

	//
	// 指定されたレイヤーに存在するオブジェクトを返します。
	//
	ExEdit::Object* find(int32_t layer)
	{
		auto it = object_map.find(layer);
		if (it == object_map.end()) return nullptr;
		return it->second;
	}
}

//
// 継承先を管理します。
//
namespace heir
{
	//
	// 継承先のオブジェクトフィルタインデックスです。
	//
	ExEdit::ObjectFilterIndex processing = {};

	//
	// 継承元のオブジェクトフィルタインデックスです。
	//
	ExEdit::ObjectFilterIndex fake = {};

	//
	// 継承先をリセットします。
	//
	void reset()
	{
		processing = {};
		fake = {};
	}

	//
	// 継承先をセットします。
	//
	void set(ExEdit::Object* processing_object, int32_t processing_filter_index, int32_t fake_filter_index)
	{
		processing = exin.get_object_filter_index(processing_object, processing_filter_index);
		fake = exin.get_object_filter_index(processing_object, fake_filter_index);
	}
}

//
// func_update()を呼び出している関数をフックします。
// 拡張編集を解析するためのコードです。実際には使用されていません。
//
namespace call_func_update
{
	//
	// この関数は拡張編集オブジェクト毎のfunc_update()です。
	//
	void (CDECL* orig_proc)(ExEdit::Object* processing_object, uint32_t u2, ExEdit::Object* processing_object2, ExEdit::FilterProcInfo* efpip) = nullptr;
	void CDECL hook_proc(ExEdit::Object* processing_object, uint32_t u2, ExEdit::Object* processing_object2, ExEdit::FilterProcInfo* efpip)
	{
		inheritance::add(processing_object->layer_set, processing_object);

		return orig_proc(processing_object, u2, processing_object2, efpip);
	}

	//
	// 初期化処理です。
	// フックをセットします。
	//
	BOOL init()
	{
		return hook::attach(orig_proc, hook_proc, 0x4A290);
	}

	//
	// 後始末処理です。
	// フックを解除します。
	//
	BOOL exit()
	{
		return hook::detach(orig_proc, hook_proc);
	}
}

//
// func_proc()を呼び出している関数をフックします。
//
namespace call_func_proc
{
	//
	// 指定されたオブジェクトが持つ描画フィルタを返します。
	//
	int32_t find_draw_filter_index(ExEdit::Object* object)
	{
		for (int32_t i = 0; i < ExEdit::Object::MAX_FILTER; i++)
		{
			auto back_index = ExEdit::Object::MAX_FILTER - i - 1;

			if (object->filter_param[back_index].is_valid())
				return back_index; // 最後尾のフィルタのインデックスを返します。
		}

		return ExEdit::Object::FilterParam::None;
	}

	//
	// 指定されたオブジェクトが持つ継承元フィルタを返します。
	//
	ExEdit::Filter* find_inheritance_filter(ExEdit::Object* object)
	{
		auto leader = exin.get_midpt_leader(object);

		for (int32_t i = 0; i < ExEdit::Object::MAX_FILTER; i++)
		{
			auto filter = exin.get_filter(object, i);
			if (!filter) return nullptr;
			if (!filter->name) continue;
			if (strcmp(filter->name, plugin_name)) continue;
			if (!(leader->filter_status[i] & ExEdit::Object::FilterStatus::Active)) continue;

			return filter;
		}

		return nullptr;
	}

	//
	// 指定されたオブジェクトが持つテキストフィルタを返します。
	//
	ExEdit::Filter* find_text_filter(ExEdit::Object* object)
	{
		// テキストフィルタのインデックスです。
		constexpr int32_t text_filter_index = 0;

		auto filter = exin.get_filter(object, text_filter_index);
		if (!filter) return nullptr;
		if (!filter->name) return nullptr;
		if (strcmp(filter->name, "テキスト")) return nullptr;

		return filter;
	}

	//
	// テキストオブジェクトの拡張データを返します。
	//
	auto get_text_exdata(ExEdit::Object* object)
	{
		// テキストフィルタのインデックスです。
		constexpr int32_t text_filter_index = 0;

		return (ExEdit::Exdata::efText*)exin.get_exdata(object, text_filter_index);
	}

	//
	// テキストオブジェクト(のリーダー)のテキストを返します。
	//
	std::vector<wchar_t> get_text(ExEdit::Object* object)
	{
		auto leader = exin.get_midpt_leader(object);
		auto exdata = get_text_exdata(leader);
		return { std::begin(exdata->text), std::end(exdata->text) };
	}

	//
	// テキストオブジェクト(のリーダー)のテキストを変更します。
	//
	void set_text(ExEdit::Object* object, const std::vector<wchar_t>& text)
	{
		auto leader = exin.get_midpt_leader(object);
		auto exdata = get_text_exdata(leader);
		std::copy(std::begin(text), std::end(text), exdata->text);
	}

	//
	// この構造体は継承元オブジェクトの設定を
	// 一時的に書き換えて処理中オブジェクトに偽装させます。
	//
	struct Disguiser
	{
		struct Track {
			int32_t value_left;
			int32_t value_right;
			ExEdit::Object::TrackMode mode;
			int32_t param;
		};

		//
		// この構造体はフィルタ設定値へのアクセサです。
		//
		struct FilterAcc
		{
			ExEdit::Object* object;
			int32_t filter_index;
			ExEdit::Filter* filter;

			//
			// コンストラクタです。
			//
			FilterAcc(ExEdit::Object* object, int32_t filter_index)
				: object(object)
				, filter_index(filter_index)
				, filter(exin.get_filter(object, filter_index))
			{
			}

			//
			// アクセサが有効の場合はTRUEを返します。
			//
			bool is_valid() const { return !!filter; }

			//
			// トラックの値を取得します。
			//
			void get_track(int32_t index, Track& track)
			{
				int32_t track_index = object->filter_param[filter_index].track_begin + index;
				track.value_left = object->track_value_left[track_index];
				track.value_right = object->track_value_right[track_index];
				track.mode = object->track_mode[track_index];
				track.param = object->track_param[track_index];
			}

			//
			// トラックの値を設定します。
			//
			void set_track(int32_t index, const Track& track)
			{
				int32_t track_index = object->filter_param[filter_index].track_begin + index;
				object->track_value_left[track_index] = track.value_left;
				object->track_value_right[track_index] = track.value_right;
				object->track_mode[track_index] = track.mode;
				object->track_param[track_index] = track.param;
			}

			//
			// チェックの値を取得します。
			//
			void get_check(int32_t index, int32_t& check)
			{
				int32_t check_index = object->filter_param[filter_index].check_begin + index;
				check = object->check_value[check_index];
			}

			//
			// チェックの値を設定します。
			//
			void set_check(int32_t index, int32_t check)
			{
				int32_t check_index = object->filter_param[filter_index].check_begin + index;
				object->check_value[check_index] = check;
			}
		};

		//
		// この構造体は変更対象のオブジェクトの値です。
		//
		struct ObjectSettings
		{
			std::vector<wchar_t> text;
			int32_t frame_begin = 0;
			int32_t frame_end = 0;
			int32_t layer_disp = 0;
			int32_t layer_set = 0;
			int32_t scene_set = 0;

			// 標準描画の場合 track_n = 6, check_n = 1, pos[3], scale, alpha, angle;
			// 拡張描画の場合 track_n = 12, check_n = 2, pos[3], scale, alpha, aspect, angle[3], origin[3];
			struct LastFilter {
				std::unique_ptr<FilterAcc> acc;
				Track pos[3] = {};
				Track scale = {};
				Track alpha = {};
				Track aspect = {};
				Track angle[3] = {};
				Track origin[3] = {};
				int32_t check[2] = {};

				//
				// 指定されたオブジェクトから値を読み込みます。
				//
				void read(ExEdit::Object* object)
				{
					acc = std::make_unique<FilterAcc>(object, find_draw_filter_index(object));

					if (strcmp(acc->filter->name, "標準描画") == 0)
					{
						acc->get_track(0, pos[0]);
						acc->get_track(1, pos[1]);
						acc->get_track(2, pos[2]);
						acc->get_track(3, scale);
						acc->get_track(4, alpha);
						acc->get_track(5, angle[2]);
						acc->get_check(0, check[0]);
					}
					else if (strcmp(acc->filter->name, "拡張描画") == 0)
					{
						acc->get_track(0, pos[0]);
						acc->get_track(1, pos[1]);
						acc->get_track(2, pos[2]);
						acc->get_track(3, scale);
						acc->get_track(4, alpha);
						acc->get_track(5, aspect);
						acc->get_track(6, angle[0]);
						acc->get_track(7, angle[1]);
						acc->get_track(8, angle[2]);
						acc->get_track(9, origin[0]);
						acc->get_track(10, origin[1]);
						acc->get_track(11, origin[2]);
						acc->get_check(0, check[0]);
						acc->get_check(1, check[1]);
					}
					else
					{
						acc.reset();
					}
				}

				//
				// 指定されたオブジェクトに値を書き込みます。
				//
				void write(ExEdit::Object* object)
				{
					if (!acc) return;

					auto acc = std::make_unique<FilterAcc>(object, find_draw_filter_index(object));

					if (strcmp(acc->filter->name, "標準描画") == 0)
					{
						acc->set_track(0, pos[0]);
						acc->set_track(1, pos[1]);
						acc->set_track(2, pos[2]);
						acc->set_track(3, scale);
						acc->set_track(4, alpha);
						acc->set_track(5, angle[2]);
						acc->set_check(0, check[0]);
					}
					else if (strcmp(acc->filter->name, "拡張描画") == 0)
					{
						acc->set_track(0, pos[0]);
						acc->set_track(1, pos[1]);
						acc->set_track(2, pos[2]);
						acc->set_track(3, scale);
						acc->set_track(4, alpha);
						acc->set_track(5, aspect);
						acc->set_track(6, angle[0]);
						acc->set_track(7, angle[1]);
						acc->set_track(8, angle[2]);
						acc->set_track(9, origin[0]);
						acc->set_track(10, origin[1]);
						acc->set_track(11, origin[2]);
						acc->set_check(0, check[0]);
						acc->set_check(1, check[1]);
					}
				}
			} draw_filter;

			//
			// コンストラクタです。
			//
			ObjectSettings(ExEdit::Object* object, BOOL no_inherit_draw_filter, ExEdit::Object* frame_object = nullptr)
			{
				text = get_text(object);
				frame_begin = object->frame_begin;
				frame_end = object->frame_end;
				layer_disp = object->layer_disp;
				layer_set = object->layer_set;
				scene_set = object->scene_set;

				if (no_inherit_draw_filter)
					draw_filter.read(object);

				if (frame_object)
				{
					frame_begin = frame_object->frame_begin;
					frame_end = frame_object->frame_end;
				}
			}

			//
			// 指定されたオブジェクトに設定値を適用します。
			//
			void apply(ExEdit::Object* object)
			{
				set_text(object, text);
				object->frame_begin = frame_begin;
				object->frame_end = frame_end;
				object->layer_disp = layer_disp;
				object->layer_set = layer_set;
				object->scene_set = scene_set;

				draw_filter.write(object);
			}
		};

		//
		// 偽装に必要なオブジェクトの交換パーツです。
		//
		struct ReplacementParts
		{
			ExEdit::Object all;
			std::vector<wchar_t> text;

			// 標準描画の場合 track_n = 6, check_n = 1, pos[3], scale, alpha, angle;
			// 拡張描画の場合 track_n = 12, check_n = 2, pos[3], scale, alpha, aspect, angle[3], origin[3];
			struct DrawFilter {
				std::unique_ptr<FilterAcc> acc;
				Track pos[3] = {};
				Track scale = {};
				Track alpha = {};
				Track aspect = {};
				Track angle[3] = {};
				Track origin[3] = {};
				int32_t check[2] = {};
				ExEdit::ObjectFilterIndex processing = {};

				//
				// 指定されたオブジェクトから値を読み込みます。
				//
				void read(ExEdit::Object* object)
				{
					acc = std::make_unique<FilterAcc>(object, find_draw_filter_index(object));

					if (strcmp(acc->filter->name, "標準描画") == 0)
					{
						acc->get_track(0, pos[0]);
						acc->get_track(1, pos[1]);
						acc->get_track(2, pos[2]);
						acc->get_track(3, scale);
						acc->get_track(4, alpha);
						acc->get_track(5, angle[2]);
						acc->get_check(0, check[0]);
						processing = acc->filter->processing;
					}
					else if (strcmp(acc->filter->name, "拡張描画") == 0)
					{
						acc->get_track(0, pos[0]);
						acc->get_track(1, pos[1]);
						acc->get_track(2, pos[2]);
						acc->get_track(3, scale);
						acc->get_track(4, alpha);
						acc->get_track(5, aspect);
						acc->get_track(6, angle[0]);
						acc->get_track(7, angle[1]);
						acc->get_track(8, angle[2]);
						acc->get_track(9, origin[0]);
						acc->get_track(10, origin[1]);
						acc->get_track(11, origin[2]);
						acc->get_check(0, check[0]);
						acc->get_check(1, check[1]);
						processing = acc->filter->processing;
					}
					else
					{
						acc.reset();
					}
				}

				//
				// 指定されたオブジェクトに値を書き込みます。
				//
				void write(ExEdit::Object* object)
				{
					if (!acc) return;

					auto acc = std::make_unique<FilterAcc>(object, find_draw_filter_index(object));

					if (strcmp(acc->filter->name, "標準描画") == 0)
					{
						acc->set_track(0, pos[0]);
						acc->set_track(1, pos[1]);
						acc->set_track(2, pos[2]);
						acc->set_track(3, scale);
						acc->set_track(4, alpha);
						acc->set_track(5, angle[2]);
						acc->set_check(0, check[0]);
					}
					else if (strcmp(acc->filter->name, "拡張描画") == 0)
					{
						acc->set_track(0, pos[0]);
						acc->set_track(1, pos[1]);
						acc->set_track(2, pos[2]);
						acc->set_track(3, scale);
						acc->set_track(4, alpha);
						acc->set_track(5, aspect);
						acc->set_track(6, angle[0]);
						acc->set_track(7, angle[1]);
						acc->set_track(8, angle[2]);
						acc->set_track(9, origin[0]);
						acc->set_track(10, origin[1]);
						acc->set_track(11, origin[2]);
						acc->set_check(0, check[0]);
						acc->set_check(1, check[1]);
					}
				}
			} draw_filter;

			//
			// コンストラクタです。
			//
			ReplacementParts(ExEdit::Object* object, BOOL no_inherit_draw_filter)
				: all(*object)
				, text(get_text(object))
			{
				if (no_inherit_draw_filter)
					draw_filter.read(object);
			}

			//
			// 指定されたオブジェクトに交換パーツを適用します。
			//
			void apply(ExEdit::Object* object, ExEdit::Object* frame_object)
			{
				set_text(object, text);
				object->layer_disp = all.layer_disp;
				object->frame_begin = all.frame_begin;
				object->frame_end = all.frame_end;
				object->index_midpt_leader = all.index_midpt_leader;
				object->group_belong = all.group_belong;
				object->layer_set = all.layer_set;
				object->scene_set = all.scene_set;

				if (frame_object)
				{
					object->frame_begin = frame_object->frame_begin;
					object->frame_end = frame_object->frame_end;
				}

				draw_filter.write(object);
			}
		};

		ExEdit::Object* processing_object = nullptr;
		std::shared_ptr<inheritance::Node> node;
		std::unique_ptr<ReplacementParts> processing_parts;
		std::unique_ptr<ReplacementParts> inheritance_parts;
		ExEdit::Object* frame_object = nullptr;
		ExEdit::Object* target_object = nullptr;

		//
		// コンストラクタです。
		// 継承元オブジェクトを書き換えます。
		//
		Disguiser(ExEdit::Object* processing_object, const std::shared_ptr<inheritance::Node>& node)
			: processing_object(processing_object)
			, node(node)
		{
			// あとで元に戻せるように交換パーツを取得しておきます。
			processing_parts = std::make_unique<ReplacementParts>(processing_object, node->no_inherit_draw_filter);
			inheritance_parts = std::make_unique<ReplacementParts>(node->object, node->no_inherit_draw_filter);

			// フレーム位置を保有するオブジェクトを取得します。
			auto frame_object = inheritance::find(node->frame_layer);

			// 描画フィルタを継承しない場合は
			if (node->no_inherit_draw_filter)
			{
				// 処理中オブジェクトを継承元オブジェクトに偽装します。
				*processing_object = inheritance_parts->all;
				processing_parts->apply(processing_object, frame_object);
				target_object = processing_object;

				// 継承先をセットします。
				heir::set(processing_object,
					processing_parts->draw_filter.acc->filter_index,
					inheritance_parts->draw_filter.acc->filter_index);
			}
			else
			{
				// 継承元オブジェクトを処理中オブジェクトに偽装します。
				processing_parts->apply(node->object, frame_object);
				target_object = node->object;
			}
		}

		//
		// デストラクタです。
		// 継承元オブジェクトの設定を元に戻します。
		//
		~Disguiser()
		{
			// 継承先をリセットします。
			heir::reset();

			// 描画フィルタを継承しない場合は
			if (node->no_inherit_draw_filter)
			{
				// 処理中オブジェクトの交換パーツを元に戻します。
				inheritance_parts->apply(processing_object, nullptr);
				*processing_object = processing_parts->all;
			}
			else
			{
				// 継承元オブジェクトの交換パーツを元に戻します。
				inheritance_parts->apply(node->object, nullptr);
			}
		}
	};

	//
	// この関数は拡張編集オブジェクト毎のfunc_proc()です。
	//
	void (CDECL* orig_proc)(ExEdit::Object* processing_object, ExEdit::FilterProcInfo* efpip, uint32_t flags) = nullptr;
	void CDECL hook_proc(ExEdit::Object* processing_object, ExEdit::FilterProcInfo* efpip, uint32_t flags)
	{
		// フラグが立っている場合は
		if (flags)
		{
			// 個別オブジェクトなどなのでデフォルト処理を実行します。
			return orig_proc(processing_object, efpip, flags);
		}

		// 継承先をリセットします。
		heir::reset();

		// このレイヤーとオブジェクトを関連付けます。
		inheritance::add(processing_object->layer_set, processing_object);

		// 処理中オブジェクトに継承元フィルタを持つ場合は
		if (auto filter = find_inheritance_filter(processing_object))
		{
			// 継承元ノードをコレクションに追加します。
			inheritance::add(processing_object, filter);

			// このオブジェクトは処理しないようにします。
			return;
		}

		// 処理中オブジェクトがテキストフィルタを持つ場合は
		if (find_text_filter(processing_object))
		{
			// 処理中オブジェクトを対象としている継承元ノードを取得します。
			if (const auto& node = inheritance::find(processing_object, efpip))
			{
				// オブジェクトを偽装します。
				Disguiser disguiser(processing_object, node);

				// 偽装したオブジェクトを拡張編集に渡して処理させます。
				return orig_proc(disguiser.target_object, efpip, flags);
			}
		}

		// デフォルト処理を実行します。
		return orig_proc(processing_object, efpip, flags);
	}

	//
	// 初期化処理です。
	// フックをセットします。
	//
	BOOL init()
	{
		return hook::attach(orig_proc, hook_proc, 0x49370);
	}

	//
	// 後始末処理です。
	// フックを解除します。
	//
	BOOL exit()
	{
		return hook::detach(orig_proc, hook_proc);
	}
}

//
// 拡張編集フィルタに値をセットする関数をフックします。
//
namespace set_filter_variables
{
	//
	// この関数は拡張編集フィルタに値をセットします。
	//
	void (CDECL* orig_proc)(ExEdit::Object* object, int32_t filter_index, int32_t* track, uint32_t u4, uint32_t u5, ExEdit::FilterProcInfo* efpip) = nullptr;
	void CDECL hook_proc(ExEdit::Object* object, int32_t filter_index, int32_t* track, uint32_t u4, uint32_t u5, ExEdit::FilterProcInfo* efpip)
	{
		// まず、デフォルト処理でフィルタに値をセットします。
		orig_proc(object, filter_index, track, u4, u5, efpip);

		// フィルタを取得できた場合は
		if (auto filter = exin.get_filter(object, filter_index))
		{
			// 継承先を監視している場合は
			if (is_valid(filter->processing) && is_valid(heir::fake))
			{
				// オブジェクトフィルタインデックスがフェイクになっている場合は
				if (filter->processing == heir::fake)
				{
					// 元のオブジェクトフィルタインデックスに戻します。
					filter->processing = heir::processing;
				}
			}
		}
	}

	//
	// 初期化処理です。
	// フックをセットします。
	//
	BOOL init()
	{
		return hook::attach(orig_proc, hook_proc, 0x47E30);
	}

	//
	// 後始末処理です。
	// フックを解除します。
	//
	BOOL exit()
	{
		return hook::detach(orig_proc, hook_proc);
	}
}

//
// ビジュアルを追加する関数をフックします。
//
namespace add_visual
{
	//
	// この関数はビジュアルを追加します。
	//
	void (CDECL* orig_proc)(ExEdit::ObjectFilterIndex object_filter_index,
		int32_t x, int32_t y, int32_t w, int32_t h,
		int16_t u6, int16_t u7, int16_t u8, int16_t u9, uint32_t flags, uint32_t id) = nullptr;
	void CDECL hook_proc(ExEdit::ObjectFilterIndex object_filter_index,
		int32_t x, int32_t y, int32_t w, int32_t h,
		int16_t u6, int16_t u7, int16_t u8, int16_t u9, uint32_t flags, uint32_t id)
	{
		// 継承先を監視している場合は
		if (is_valid(object_filter_index) && is_valid(heir::fake))
		{
			// オブジェクトフィルタインデックスがフェイクになっている場合は
			if (object_filter_index == heir::fake)
			{
				// 元のオブジェクトフィルタインデックスに戻します。
				object_filter_index = heir::processing;
			}
		}

		// デフォルト処理を実行します。
		orig_proc(object_filter_index, x, y, w, h, u6, u7, u8, u9, flags, id);
	}

	//
	// 初期化処理です。
	// フックをセットします。
	//
	BOOL init()
	{
		return hook::attach(orig_proc, hook_proc, 0x4BD60);
	}

	//
	// 後始末処理です。
	// フックを解除します。
	//
	BOOL exit()
	{
		return hook::detach(orig_proc, hook_proc);
	}
}

//
// この構造体は登録するフィルタ情報を管理します。
//
inline struct {
	inline static constexpr int32_t filter_n = 1;

	//
	// この構造体は設定共通化フィルタです。
	//
	struct Inheritance {
		//
		// トラックの定義です。
		//
		inline static constexpr int32_t track_n = 2;
		struct Track {
			LPCSTR name;
			int32_t default_value;
			int32_t min_value;
			int32_t max_value;
			int32_t scale;
		} track[track_n] = {
			{ "ﾚｲﾔｰ数", 1, 0, 100 },
			{ "ﾌﾚｰﾑﾚｲﾔｰ", 0, 0, 100 },
		};
		LPCSTR track_name[track_n] = {};
		int32_t track_default[track_n] = {};
		int32_t track_s[track_n] = {};
		int32_t track_e[track_n] = {};

		//
		// チェックの定義です。
		//
		inline static constexpr int32_t check_n = 2;
		struct Check {
			LPCSTR name;
			int32_t default_value;
		} check[check_n] = {
			{ "同じグループのオブジェクトを対象にする", FALSE },
			{ "標準描画・拡張描画は継承しない", FALSE },
		};
		LPCSTR check_name[check_n] = {};
		int32_t check_default[check_n] = {};

		//
		// フィルタの定義です。
		//
		ExEdit::Filter filter = {
			.flag = ExEdit::Filter::Flag::Effect,
			.name = plugin_name,
			.track_n = track_n,
			.track_name = const_cast<char**>(track_name),
			.track_default = track_default,
			.track_s = track_s,
			.track_e = track_e,
			.check_n = check_n,
			.check_name = const_cast<char**>(check_name),
			.check_default = check_default,
			.func_proc = func_proc,
			.func_init = func_init,
			.func_update = func_update,
		};

		//
		// コンストラクタです。
		//
		Inheritance()
		{
			// トラックデータを個別の配列に変換します。
			init_array(track_name, track, &Track::name);
			init_array(track_default, track, &Track::default_value);
			init_array(track_s, track, &Track::min_value);
			init_array(track_e, track, &Track::max_value);

			// チェックデータを個別の配列に変換します。
			init_array(check_name, check, &Check::name);
			init_array(check_default, check, &Check::default_value);
		}

		template <typename T0, size_t N, typename T1, typename T2>
		inline static void init_array(T0 (&t0)[N], const T1& t1, T2 t2)
		{
			for (size_t i = 0; i < N; i++)
				t0[i] = t1[i].*t2;
		}

		inline static BOOL func_init(ExEdit::Filter* efp)
		{
			exin.init();
//			call_func_update::init();
			call_func_proc::init();
			set_filter_variables::init();
			add_visual::init();

			return TRUE;
		}

		inline static BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip)
		{
			return TRUE;
		}

		inline static BOOL func_update(ExEdit::Filter* efp, int32_t status)
		{
			switch (status)
			{
			case 1: // pre_workflow
			case 2: // post_workflow
				{
					// 継承元をリセットします。
					// (ここは拡張編集の描画ワークフローの最初と最後に呼び出されます)
					inheritance::reset();

					break;
				}
			case 3: // pre_func_proc
				{
					// ここでTRUEを返すと、このフィルタが付与されている
					// オブジェクトが描画されなくなります。(func_proc()が呼ばれなくなります)
					break;
				}
			}

			return FALSE;
		}
	} inheritance;

	ExEdit::Filter* filter_list[filter_n + 1] = {
		&inheritance.filter,
		nullptr,
	};
} registrar;

//
// フィルタ登録関数です。
// エクスポート関数です。
// patch.aulから呼び出されます。
//
EXTERN_C ExEdit::Filter** WINAPI GetFilterTableList()
{
	return registrar.filter_list;
}
