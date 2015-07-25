#ifndef NEW_FIELD_H
#define NEW_FIELD_H

#include <string>
#include <unordered_set>
#include <vector>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "FieldModifier.h"
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
	size_t calc_state(double quantization, double beat, bool vivid) const
	{
		QuantizedStates const& quantum= calc_quantization(quantization);
		size_t frame_index= static_cast<size_t>(
			floor(((vivid ? quantization : 0.0) + beat) * quantum.states.size()))
			% quantum.states.size();
		return quantum.states[frame_index];
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
	Actor* get_quantized(double quantization, double beat, double rotation)
	{
		const size_t state= m_state_map.calc_state(quantization, beat, m_vivid);
		m_actor->SetState(state);
		m_actor->SetBaseRotationZ(rotation);
		// Return the frame and not the actor because the notefield is going to
		// apply mod transforms to it.  Returning the actor would make the mod
		// transform stomp on the rotation the noteskin supplies.
		return &m_frame;
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

struct QuantizedHoldRenderData
{
	QuantizedHoldRenderData() { clear(); }
	std::vector<RageTexture*> parts;
	RectF const* rect;
	TexCoordFlipMode flip;
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
	TexCoordFlipMode m_flip;
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
		ret.flip= m_flip;
	}
	bool load_from_lua(lua_State* L, int index, std::string const& load_dir, std::string& insanity_diagnosis);
};

struct NewSkinColumn
{
	Actor* get_tap_actor(NewSkinTapPart type, double quantization, double beat);
	Actor* get_optional_actor(NewSkinTapOptionalPart type, double quantization,
		double beat);
	void get_hold_render_data(TapNoteSubType sub_type, bool active,
		bool reverse, double quantization, double beat,
		QuantizedHoldRenderData& data);
	bool load_holds_from_lua(lua_State* L, int index,
		std::vector<std::vector<QuantizedHold> >& holder,
		std::string const& holds_name,
		std::string const& load_dir, std::string& insanity_diagnosis);
	bool load_from_lua(lua_State* L, int index, std::string const& load_dir,
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
	void transform_columns(std::vector<transform>& positions);
	void pass_message_to_column(size_t column, Message const& msg)
	{
		if(column < m_actors.size())
		{
			m_actors[column]->HandleMessage(msg);
		}
	}

private:
	// The actors have to be wrapped inside of frames so that mod transforms
	// can be applied without stomping the rotation the noteskin supplies.
	std::vector<ActorFrame> m_frames;
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
	void transform_columns(std::vector<transform>& positions)
	{
		for(auto&& lay : m_layers_below_notes)
		{
			lay.transform_columns(positions);
		}
		for(auto&& lay : m_layers_above_notes)
		{
			lay.transform_columns(positions);
		}
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

	void get_hold_draw_time(TapNote const& tap, double const hold_beat, double& beat, double& second);
	void draw_hold(QuantizedHoldRenderData& data, double head_beat,
		double head_second, double len);
	void update_displayed_beat(double beat, double second);
	bool y_offset_visible(double off)
	{
		return off >= first_y_offset_visible && off <= last_y_offset_visible;
	}
	double calc_y_offset(double beat, double second);
	double quantization_for_beat(double beat)
	{
		double second= m_timing_data->GetElapsedTimeFromBeat(beat);
		mod_val_inputs input(beat, second, m_curr_beat, m_curr_second);
		double mult= m_quantization_multiplier.evaluate(input);
		double offset= m_quantization_offset.evaluate(input);
		return fmodf((beat * mult) + offset, 1.0);
	}
	void calc_transform(double beat, double second, transform& trans);
	void calc_transform_for_head(transform& trans)
	{
		double y_offset= calc_y_offset(m_curr_beat, m_curr_second);
		double render_y= apply_reverse_shift(y_offset);
		calc_transform(m_curr_beat, m_curr_second, trans);
		trans.pos.x+= GetX();
		trans.pos.y+= GetY() + render_y;
		trans.pos.z+= GetZ();
		trans.rot.x+= GetRotationX();
		trans.rot.y+= GetRotationY();
		trans.rot.z+= GetRotationZ();
		trans.zoom.x*= GetZoomX() * GetBaseZoomX();
		trans.zoom.y*= GetZoomY() * GetBaseZoomY();
		trans.zoom.z*= GetZoomZ() * GetBaseZoomZ();
	}
	void calc_reverse_shift();
	double apply_reverse_shift(double y_offset);
	void build_render_lists();
	void draw_holds();
	void draw_taps();
	void draw_children();

	virtual void UpdateInternal(float delta);
	virtual bool EarlyAbortDraw() const;
	void update_upcoming(int row, double dist_factor);
	void update_active_hold(TapNote const& tap);
	virtual void DrawPrimitives();

	virtual void PushSelf(lua_State *L);
	virtual NewFieldColumn* Copy() const;

	bool m_use_game_music_beat;

	struct column_status
	{
		column_status()
			:active_hold(nullptr), prev_active_hold(nullptr)
		{}
		double dist_to_upcoming_arrow;
		TapNote const* active_hold;
		TapNote const* prev_active_hold;
	};
	column_status m_status;

	ModManager m_mod_manager;
	ModifiableValue m_quantization_multiplier;
	ModifiableValue m_quantization_offset;

	ModifiableValue m_speed_mod;

	ModifiableValue m_reverse_offset_pixels;
	ModifiableValue m_reverse_percent;
	ModifiableValue m_center_percent;

	ModifiableTransform m_note_mod;
	ModifiableTransform m_column_mod;

private:
	double m_curr_beat;
	double m_curr_second;
	double m_pixels_visible_before_beat;
	double m_pixels_visible_after_beat;
	size_t m_column;
	NewSkinColumn* m_newskin;
	const NoteData* m_note_data;
	const TimingData* m_timing_data;
	// Data that needs to be stored for rendering below here.
	// Holds and taps are put into different lists because they have to be
	// rendered in different phases.  All hold bodies must be drawn first, then
	// all taps, so the taps appear on top of the hold bodies and are not
	// obscured.
	void draw_holds_internal();
	void draw_taps_internal();
	enum render_step
	{
		RENDER_HOLDS,
		RENDER_TAPS,
		RENDER_CHILDREN
	};
	std::vector<NoteData::TrackMap::const_iterator> render_holds;
	std::vector<NoteData::TrackMap::const_iterator> render_taps;
	render_step curr_render_step;
	// Calculating the effects of reverse and center for every note is costly.
	// Only do it once per frame and store the result.
	double reverse_shift;
	double reverse_scale;
	double reverse_scale_sign;
	double first_y_offset_visible;
	double last_y_offset_visible;
};

struct NewField : ActorFrame
{
	NewField();
	~NewField();
	virtual void UpdateInternal(float delta);
	virtual bool EarlyAbortDraw() const;
	virtual void PreDraw();
	virtual void DrawPrimitives();

	void draw_layer_set(std::vector<NewSkinLayer>& layers);

	virtual void PushSelf(lua_State *L);
	virtual NewField* Copy() const;

	void push_columns_to_lua(lua_State* L);

	void clear_steps();
	void set_steps(Steps* data);
	void set_note_data(NoteData* note_data, TimingData const* timing, Style const* curr_style);

	void update_displayed_beat(double beat, double second);

	void did_tap_note(size_t column, TapNoteScore tns, bool bright);
	void did_hold_note(size_t column, HoldNoteScore hns, bool bright);
	void set_hold_status(size_t column, TapNote const* tap, bool start, bool end);
	void set_pressed(size_t column, bool on);
	void set_note_upcoming(size_t column, double distance);

	ModManager m_mod_manager;
	ModifiableTransform m_trans_mod;

private:
	double m_curr_beat;
	double m_curr_second;

	bool m_own_note_data;
	NoteData* m_note_data;
	const TimingData* m_timing_data;
	std::vector<NewFieldColumn> m_columns;
	NewSkinData m_newskin;
	NewSkinLoader m_skin_walker;
};

#endif
