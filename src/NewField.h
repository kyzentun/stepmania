#ifndef NEW_FIELD_H
#define NEW_FIELD_H

#include <string>
#include <unordered_set>
#include <vector>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "NoteData.h"
#include "RageTexture.h"

class NoteData;
class Steps;
class TimingData;

// Receptors and explosions are full actors.  There are a fixed number of
// them, and that number is relatively small.  Their update functions will
// be called each frame.
// Taps are actors that occur at a single point in time.  One is made for
// each NewSkinTapPart and NewSkinTapOptionalPart, and that one is reused
// whenever a tap of that part is needed.
// Everything in Tap and Hold is considered quantizable.  They get a
// state map to control what part of their texture is used at a given
// quantization and beat.
// Everything in Tap also has its base rotation controlled by the field,
// so they are automatically rotated to the column.
// Holds are loaded by the tap loader, so there isn't a separate enum entry
// for holds.
// Holds must be stretched over a period, so they are not actors at all.
// Instead, they only have 6 textures: the two caps and the body, in active
// and inactive states.  These textures are then rendered to generated
// quads.

enum NewSkinTapPart
{
	// These tap parts must be provided by the noteskin.  If they are absent,
	// it is an error.
	NSTP_Tap,
	NSTP_Mine,
	NSTP_Lift,
	NUM_NewSkinTapPart,
	NewSkinTapPart_Invalid
};
const RString& NewSkinTapPartToString(NewSkinTapPart nsp);
LuaDeclareType(NewSkinTapPart);

enum NewSkinTapOptionalPart
{
	// These tap parts are optional.  If none of them exist, nothing is used.
	// If HoldHead exists and RollHead does not, HoldHead is used when a
	// RollHead is needed.
	NSTOP_HoldHead,
	NSTOP_HoldTail,
	NSTOP_RollHead,
	NSTOP_RollTail,
	NSTOP_CheckpointHead,
	NSTOP_CheckpointTail,
	NUM_NewSkinTapOptionalPart,
	NewSkinTapOptionalPart_Invalid
};
const RString& NewSkinTapOptionalPartToString(NewSkinTapOptionalPart nsp);
LuaDeclareType(NewSkinTapOptionalPart);

enum NewSkinHoldPart
{
	NSHP_Top,
	NSHP_Body,
	NSHP_Bottom,
	NUM_NewSkinHoldPart,
	NewSkinHoldPart_Invalid
};
const RString& NewSkinHoldPartToString(NewSkinHoldPart nsp);
LuaDeclareType(NewSkinHoldPart);

struct QuantizedStateMap
{
	// This is used for both taps and holds, thus, holds can be colored by
	// quantization too.
	// Notes need to have two things affecting their state:
	// 1. The quantization.  This is the beat the note is on.
	// 2. The beat.  This is the current beat of the music.
	// The range of both is [0 to 1), 0 is included in the range, 1 is not.
	// For a vivid style noteskin, the quantization and the beat are added
	// together to calculate the time into the animation.
	// For a note style noteskin, the quantization is used to pick a set of
	// states, then the beat is the time into that set of states.
	// All the states to use are stored in m_states.  Indexing occurs in two
	// stages:
	// 1. If the noteskin is vivid style, there is only one entry in m_states.
	//   Otherwise, the entries are equally spread over the quantization range.
	//   floor(quantization * m_states.size()) is the index into m_states if the
	//   noteskin is not vivid.
	// 2. If the noteskin is vivid, the index into the entry is this:
	//   floor((quantization + beat) * entry.size())
	//   If the noteskin is not vivid, the index into the entry is this:
	//   floor(beat * entry.size())
	//

	// Worst case for the size of m_states:  48 quantizations, 16 frames per
	// quantization, 8 bytes per state works out to 6144 bytes.
	// If there are 10 buttons per noteskin 4 noteskins loaded in the game at
	// the same time, that works out to 245760 bytes.

	QuantizedStateMap()
	{
		clear();
	}

	// probably a sane limit, anybody going over 256 probably made a mistake in
	// a loop that generates the map.
	static const size_t max_state_map_entries= 256;

	size_t calc_state(double quantization, double beat, bool vivid) const
	{
		// Vivid can be ignored for calculating entry_index because there should
		// be only one entry in m_states if the thing is vivid.
		// Add half an entry to the quantization so that notes are quantized to
		// the closest entry.  Otherwise, .20 would quantize to 0 with 4 entries.
		size_t entry_index= static_cast<size_t>(
			floor((quantization * m_states.size()) + .5)) % m_states.size();
		std::vector<size_t> const& entry= m_states[entry_index];
		// Multiplying by a bool is perfectly legal, the bool is either 0 or 1.
		size_t state_index= static_cast<size_t>(
			floor(((quantization * vivid) + beat) * entry.size())) % entry.size();
		return entry[state_index];
	}
	void clear()
	{
		m_states.resize(1);
		m_states[0].resize(1);
		m_states[0][0]= 0;
	}
	bool load_from_lua(lua_State* L, int index, std::string& insanity_diagnosis);
	void swap(QuantizedStateMap& other)
	{
		m_states.swap(other.m_states);
	}
private:
	std::vector<std::vector<size_t> > m_states;
};

struct QuantizedTap
{
	QuantizedStateMap m_state_map;
	AutoActor m_actor;
	bool m_vivid;
	Actor* get_quantized(double quantization, double beat)
	{
		const size_t state= m_state_map.calc_state(quantization, beat, m_vivid);
		m_actor->SetState(state);
		return m_actor;
	}
	bool load_from_lua(lua_State* L, int index, std::string& insanity_diagnosis);
};

struct QuantizedHoldRenderData
{
	QuantizedHoldRenderData() { clear(); }
	std::vector<RageTexture*> parts;
	RectF const* rect;
	void clear()
	{
		parts.clear();
		rect= nullptr;
	}
};

struct QuantizedHold
{
	static const size_t max_hold_layers= 32;
	QuantizedStateMap m_state_map;
	std::vector<RageTexture*> m_parts;
	bool m_vivid;
	void get_quantized(double quantization, double beat, QuantizedHoldRenderData& ret)
	{
		const size_t state= m_state_map.calc_state(quantization, beat, m_vivid);
		for(size_t i= 0; i < m_parts.size(); ++i)
		{
			ret.parts.push_back(m_parts[i]);
			if(ret.rect == nullptr)
			{
				ret.rect= m_parts[i]->GetTextureCoordRect(state);
			}
		}
	}
	bool load_from_lua(lua_State* L, int index, std::string const& load_dir, std::string& insanity_diagnosis);
};

struct NewSkinColumn
{
	Actor* get_tap_actor(NewSkinTapPart type, double quantization, double beat);
	Actor* get_optional_actor(NewSkinTapOptionalPart type, double quantization, double beat);
	void get_hold_render_data(TapNoteSubType sub_type, bool active, double quantization, double beat, QuantizedHoldRenderData& data);
	bool load_from_lua(lua_State* L, int index, std::string const& load_dir, std::string& insanity_diagnosis);
	void vivid_operation(bool vivid)
	{
		for(auto&& tap : m_taps)
		{
			tap.m_vivid= vivid;
		}
		for(auto&& tap : m_optional_taps)
		{
			if(tap != nullptr)
			{
				tap->m_vivid= vivid;
			}
		}
		for(auto&& subtype : m_holds)
		{
			for(auto&& action : subtype)
			{
				action.m_vivid= vivid;
			}
		}
	}
	void clear_optionals()
	{
		for(auto&& tap : m_optional_taps)
		{
			if(tap != nullptr)
			{
				SAFE_DELETE(tap);
			}
		}
	}
	NewSkinColumn()
		:m_optional_taps(NUM_NewSkinTapOptionalPart, nullptr)
	{}
	~NewSkinColumn()
	{
		clear_optionals();
	}
private:
	// m_taps is indexed by NewSkinTapPart.
	std::vector<QuantizedTap> m_taps;
	// m_optional_taps is indexed by NewSkinTapOptionalPart.
	// If an entry is null, the skin doesn't use that part.
	std::vector<QuantizedTap*> m_optional_taps;
	// Dimensions of m_holds:
	// note subtype, active/inactive.
	std::vector<std::vector<QuantizedHold> > m_holds;
	// m_rotation_factors stores the amount to rotate each NSTP.
	// So the noteskin can set taps to rotate 90 degrees in this column and
	// mines to rotate 0, and taps will be rotated and mines won't.
	std::vector<double> m_rotations;
};

struct NewSkinLayer : ActorFrame
{
	virtual void UpdateInternal(float delta);
	virtual void DrawPrimitives();

	bool load_from_lua(lua_State* L, int index, size_t columns, std::string& insanity_diagnosis);
	void position_columns_to_info(std::vector<double>& positions);
	void pass_message_to_column(size_t column, Message const& msg)
	{
		if(column < m_actors.size())
		{
			m_actors[column]->HandleMessage(msg);
		}
	}

	std::vector<AutoActor> m_actors;
};

struct NewSkinData
{
	static const size_t max_columns= 256;
	NewSkinData();
	NewSkinColumn* get_column(size_t column)
	{
		if(column >= m_columns.size())
		{
			return nullptr;
		}
		return &m_columns[column];
	}
	bool load_layer_from_lua(lua_State* L, int index, bool under_notes, size_t columns, std::string& insanity_diagnosis);
	bool load_taps_from_lua(lua_State* L, int index, size_t columns, std::string const& load_dir, std::string& insanity_diagnosis);
	bool loaded_successfully() const { return m_loaded; }
	void pass_positions_to_layers(std::vector<NewSkinLayer>& layers, std::vector<double>& positions)
	{
		for(auto&& lay : layers)
		{
			lay.position_columns_to_info(positions);
		}
	}
	void pass_positions_to_all_layers(std::vector<double>& positions)
	{
		pass_positions_to_layers(m_layers_below_notes, positions);
		pass_positions_to_layers(m_layers_above_notes, positions);
	}
	void update_layers(std::vector<NewSkinLayer>& layers, float delta)
	{
		for(auto&& lay : layers)
		{
			lay.Update(delta);
		}
	}
	void update_all_layers(float delta)
	{
		update_layers(m_layers_below_notes, delta);
		update_layers(m_layers_above_notes, delta);
	}
	void pass_message_to_layers(std::vector<NewSkinLayer>& layers, size_t column, Message const& msg)
	{
		for(auto&& lay : layers)
		{
			lay.pass_message_to_column(column, msg);
		}
	}
	void pass_message_to_all_layers(size_t column, Message const& msg)
	{
		pass_message_to_layers(m_layers_below_notes, column, msg);
		pass_message_to_layers(m_layers_above_notes, column, msg);
	}

	std::vector<NewSkinLayer> m_layers_below_notes;
	std::vector<NewSkinLayer> m_layers_above_notes;
private:
	std::vector<NewSkinColumn> m_columns;
	bool m_loaded;
};

struct NewSkinLoader
{
	static const size_t max_layers= 16;
	NewSkinLoader()
		:m_supports_all_buttons(false)
	{}
	std::string const& get_name()
	{
		return m_skin_name;
	}
	bool load_from_file(std::string const& path);
	bool load_from_lua(lua_State* L, int index, std::string const& name,
		std::string const& path, std::string& insanity_diagnosis);
	bool supports_needed_buttons(std::vector<std::string> const& button_list);
	bool push_loader_function(lua_State* L, std::string const& loader);
	bool load_layer_set_into_data(lua_State* L, int button_list_index,
		size_t columns, std::vector<std::string> const& loader_set,
		std::vector<NewSkinLayer>& dest, std::string& insanity_diagnosis);
	bool load_into_data(std::vector<std::string> const& button_list,
		NewSkinData& dest, std::string& insanity_diagnosis);
private:
	std::string m_skin_name;
	std::string m_load_path;
	std::string m_notes_loader;
	std::vector<std::string> m_below_loaders;
	std::vector<std::string> m_above_loaders;
	std::unordered_set<std::string> m_supported_buttons;
	bool m_supports_all_buttons;
};

struct NewFieldColumn : ActorFrame
{
	NewFieldColumn();
	~NewFieldColumn();

	void set_column_info(size_t column, NewSkinColumn* newskin,
		const NoteData* note_data, const TimingData* timing_data, double x);

	void draw_hold(QuantizedHoldRenderData& data, double x, double y, double len);
	void update_displayed_beat(double beat);

	virtual void UpdateInternal(float delta);
	virtual bool EarlyAbortDraw() const;
	void update_upcoming(int row);
	void update_active_hold(TapNote const& tap);
	virtual void DrawPrimitives();

	virtual void PushSelf(lua_State *L);
	virtual NewFieldColumn* Copy() const;

	bool m_use_game_music_beat;
	double m_dist_to_upcoming_arrow;
	TapNote const* m_active_hold;
	TapNote const* m_prev_active_hold;
private:
	float m_curr_beat;
	size_t m_column;
	NewSkinColumn* m_newskin;
	const NoteData* m_note_data;
	const TimingData* m_timing_data;
};

struct NewField : ActorFrame
{
	NewField();
	~NewField();
	virtual void UpdateInternal(float delta);
	virtual bool EarlyAbortDraw() const;
	virtual void DrawPrimitives();

	void draw_layer_set(std::vector<NewSkinLayer>& layers);

	virtual void PushSelf(lua_State *L);
	virtual NewField* Copy() const;

	void clear_steps();
	void set_steps(Steps* data);
	void set_note_data(NoteData* note_data, TimingData const* timing, Style const* curr_style);

	void update_displayed_beat(double beat);

	void did_tap_note(size_t column, TapNoteScore tns, bool bright);
	void did_hold_note(size_t column, HoldNoteScore hns, bool bright);
	void set_hold_status(size_t column, TapNote const* tap, bool start);
	void set_pressed(size_t column, bool on);
	void set_note_upcoming(size_t column, double distance);

private:
	bool m_own_note_data;
	NoteData* m_note_data;
	const TimingData* m_timing_data;
	std::vector<NewFieldColumn> m_columns;
	NewSkinData m_newskin;
	NewSkinLoader m_skin_walker;
};

#endif
