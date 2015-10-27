#ifndef NEW_SKIN_H
#define NEW_SKIN_H

#include <unordered_set>

#include "Actor.h"
#include "ActorFrame.h"
#include "AutoActor.h"
#include "NoteTypes.h"
#include "RageTexture.h"

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

// There are three modes for playerizing notes for routine mode.
// NPM_Off is for not playerizing at all.
// NPM_Mask uses the color mask in the noteskin.
// NPM_Quanta uses the quanta in the noteskin as per-player notes.
enum NotePlayerizeMode
{
	NPM_Off,
	NPM_Mask,
	NPM_Quanta,
	NUM_NotePlayerizeMode,
	NotePlayerizeMode_Invalid
};
const RString& NotePlayerizeModeToString(NotePlayerizeMode npm);
LuaDeclareType(NotePlayerizeMode);

struct NewSkinLoader;

struct QuantizedStateMap
{
	static const size_t max_quanta= 256;
	static const size_t max_states= 256;
	// A QuantizedStateMap has a list of the quantizations the noteskin has.
	// A quantization occurs a fixed integer number of times per beat and has a
	// few states for its animation.
	struct QuantizedStates
	{
		size_t per_beat;
		std::vector<size_t> states;
	};

	QuantizedStateMap()
	{
		clear();
	}

	QuantizedStates const& calc_quantization(double quantization) const
	{
		// Real world use case for solving the fizzbuzz problem.  Find the
		// largest factor for a number from the entries in a short list.
		size_t beat_part= static_cast<size_t>((quantization * m_parts_per_beat) + .5);
		for(auto&& quantum : m_quanta)
		{
			size_t spacing= static_cast<size_t>(m_parts_per_beat / quantum.per_beat);
			if(spacing * quantum.per_beat != m_parts_per_beat)
			{
				// This quantum is finer than what is supported by the parts per
				// beat.  Skipping it allows a noteskin author to twiddle the
				// quantization of the skin by changing the parts per beat without
				// changing the list of quantizations.
				continue;
			}
			if(beat_part % spacing == 0)
			{
				return quantum;
			}
		}
		return m_quanta.back();
	}
	size_t calc_frame(QuantizedStates const& quantum, double quantization,
		double beat, bool vivid) const
	{
		size_t frame_index= static_cast<size_t>(
			floor(((vivid ? quantization : 0.0) + beat) * quantum.states.size()))
			% quantum.states.size();
		return quantum.states[frame_index];
	}
	size_t calc_state(double quantization, double beat, bool vivid) const
	{
		QuantizedStates const& quantum= calc_quantization(quantization);
		return calc_frame(quantum, quantization, beat, vivid);
	}
	size_t calc_player_state(size_t pn, double beat, bool vivid) const
	{
		QuantizedStates const& quantum= m_quanta[pn%m_quanta.size()];
		return calc_frame(quantum, 0.0, beat, vivid);
	}
	bool load_from_lua(lua_State* L, int index, std::string& insanity_diagnosis);
	void swap(QuantizedStateMap& other)
	{
		size_t tmp= m_parts_per_beat;
		m_parts_per_beat= other.m_parts_per_beat;
		other.m_parts_per_beat= tmp;
		m_quanta.swap(other.m_quanta);
	}
	void clear()
	{
		m_parts_per_beat= 1;
		m_quanta.resize(1);
		m_quanta[0]= {1, {1}};
	}
private:
	size_t m_parts_per_beat;
	std::vector<QuantizedStates> m_quanta;
};

struct QuantizedTap
{
	Actor* get_common(size_t state, double rotation)
	{
		m_actor->SetState(state);
		m_actor->SetBaseRotationZ(rotation);
		// Return the frame and not the actor because the notefield is going to
		// apply mod transforms to it.  Returning the actor would make the mod
		// transform stomp on the rotation the noteskin supplies.
		return &m_frame;
	}
	Actor* get_quantized(double quantization, double beat, double rotation)
	{
		const size_t state= m_state_map.calc_state(quantization, beat, m_vivid);
		return get_common(state, rotation);
	}
	Actor* get_playerized(size_t pn, double beat, double rotation)
	{
		const size_t state= m_state_map.calc_player_state(pn, beat, m_vivid);
		return get_common(state, rotation);
	}
	bool load_from_lua(lua_State* L, int index, std::string& insanity_diagnosis);
	bool m_vivid;
private:
	QuantizedStateMap m_state_map;
	AutoActor m_actor;
	ActorFrame m_frame;
};

enum TexCoordFlipMode
{
	TCFM_None,
	TCFM_X,
	TCFM_Y,
	TCFM_XY,
	NUM_TexCoordFlipMode,
	TexCoordFlipMode_Invalid
};
const RString& TexCoordFlipModeToString(TexCoordFlipMode tcfm);
LuaDeclareType(TexCoordFlipMode);

struct hold_part_lengths
{
	double start_note_offset;
	double end_note_offset;
	double head_pixs;
	double body_pixs;
	double tail_pixs;
};

struct QuantizedHoldRenderData
{
	QuantizedHoldRenderData() { clear(); }
	std::vector<RageTexture*> parts;
	RageTexture* mask;
	RectF const* rect;
	TexCoordFlipMode flip;
	hold_part_lengths part_lengths;
	void clear()
	{
		parts.clear();
		mask= nullptr;
		rect= nullptr;
	}
};

struct QuantizedHold
{
	static const size_t max_hold_layers= 32;
	QuantizedStateMap m_state_map;
	std::vector<RageTexture*> m_parts;
	TexCoordFlipMode m_flip;
	bool m_vivid;
	hold_part_lengths m_part_lengths;
	~QuantizedHold();
	void get_common(size_t state, QuantizedHoldRenderData& ret)
	{
		for(size_t i= 0; i < m_parts.size(); ++i)
		{
			ret.parts.push_back(m_parts[i]);
			if(ret.rect == nullptr)
			{
				ret.rect= m_parts[i]->GetTextureCoordRect(state);
			}
		}
		ret.flip= m_flip;
		ret.part_lengths= m_part_lengths;
	}
	void get_quantized(double quantization, double beat, QuantizedHoldRenderData& ret)
	{
		const size_t state= m_state_map.calc_state(quantization, beat, m_vivid);
		get_common(state, ret);
	}
	void get_playerized(size_t pn, double beat, QuantizedHoldRenderData& ret)
	{
		const size_t state= m_state_map.calc_player_state(pn, beat, m_vivid);
		get_common(state, ret);
	}
	bool load_from_lua(lua_State* L, int index, NewSkinLoader const* load_skin, std::string& insanity_diagnosis);
};

struct NewSkinColumn
{
	Actor* get_tap_actor(size_t type, double quantization, double beat);
	Actor* get_optional_actor(size_t type, double quantization, double beat);
	Actor* get_player_tap(size_t type, size_t pn, double beat);
	Actor* get_player_optional_tap(size_t type, size_t pn, double beat);
	void get_hold_render_data(TapNoteSubType sub_type,
		NotePlayerizeMode playerize_mode, size_t pn, bool active, bool reverse,
		double quantization, double beat, QuantizedHoldRenderData& data);
	double get_width() { return m_width; }
	double get_padding() { return m_padding; }
	bool supports_masking()
	{
		return !(m_hold_player_masks.empty() || m_hold_reverse_player_masks.empty());
	}
	bool load_holds_from_lua(lua_State* L, int index,
		std::vector<std::vector<QuantizedHold> >& holder,
		std::string const& holds_name,
		NewSkinLoader const* load_skin, std::string& insanity_diagnosis);
	bool load_texs_from_lua(lua_State* L, int index,
		std::vector<RageTexture*>& dest,
		std::string const& texs_name,
		NewSkinLoader const* load_skin, std::string& insanity_diagnosis);
	bool load_from_lua(lua_State* L, int index, NewSkinLoader const* load_skin,
		std::string& insanity_diagnosis);
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
		for(auto&& subtype : m_reverse_holds)
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
	std::vector<std::vector<QuantizedHold> > m_reverse_holds;
	// m_hold_player_masks is indexed by note subtype.
	std::vector<RageTexture*> m_hold_player_masks;
	std::vector<RageTexture*> m_hold_reverse_player_masks;
	// m_rotation_factors stores the amount to rotate each NSTP.
	// So the noteskin can set taps to rotate 90 degrees in this column and
	// mines to rotate 0, and taps will be rotated and mines won't.
	std::vector<double> m_rotations;
	double m_width;
	double m_padding;
};

struct NewSkinLayer
{
	bool load_from_lua(lua_State* L, int index, size_t columns, std::string& insanity_diagnosis);
	// The actors are public so that the NewFieldColumns can go through and
	// take ownership of the actors after loading.
	std::vector<Actor*> m_actors;
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
	size_t num_columns() { return m_columns.size(); }
	bool load_taps_from_lua(lua_State* L, int index, size_t columns, NewSkinLoader const* load_skin, std::string& insanity_diagnosis);
	bool loaded_successfully() const { return m_loaded; }

	// The layers are public so that the NewFieldColumns can go through and
	// take ownership of the actors after loading.
	std::vector<NewSkinLayer> m_layers_below_notes;
	std::vector<NewSkinLayer> m_layers_above_notes;
	std::vector<RageColor> m_player_colors;
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
	std::string const& get_name() const
	{
		return m_skin_name;
	}
	std::string const& get_fallback_name() const
	{
		return m_fallback_skin_name;
	}
	std::string const& get_load_path() const
	{
		return m_load_path;
	}
	bool load_from_file(std::string const& path);
	bool load_from_lua(lua_State* L, int index, std::string const& name,
		std::string const& path, std::string& insanity_diagnosis);
	bool supports_needed_buttons(StepsType stype) const;
	bool push_loader_function(lua_State* L, std::string const& loader);
	bool load_layer_set_into_data(lua_State* L, int button_list_index,
		int stype_index,
		size_t columns, std::vector<std::string> const& loader_set,
		std::vector<NewSkinLayer>& dest, std::string& insanity_diagnosis);
	bool load_into_data(StepsType stype,
		NewSkinData& dest, std::string& insanity_diagnosis);
private:
	std::string m_skin_name;
	std::string m_fallback_skin_name;
	std::string m_load_path;
	std::string m_notes_loader;
	std::vector<std::string> m_below_loaders;
	std::vector<std::string> m_above_loaders;
	std::vector<RageColor> m_player_colors;
	std::unordered_set<std::string> m_supported_buttons;
	bool m_supports_all_buttons;
};

#endif
