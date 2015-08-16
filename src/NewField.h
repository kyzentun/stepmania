#ifndef NEW_FIELD_H
#define NEW_FIELD_H

#include <string>
#include <unordered_set>
#include <vector>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "ModValue.h"
#include "NewSkin.h"
#include "NoteData.h"

class NoteData;
class Steps;
class TimingData;

struct NewFieldColumn : ActorFrame
{
	NewFieldColumn();
	~NewFieldColumn();

	struct column_head
	{
		// The actors have to be wrapped inside of frames so that mod transforms
		// can be applied without stomping the rotation the noteskin supplies.
		ActorFrame frame;
		AutoActor actor;
		void load(Actor* act)
		{
			actor.Load(act);
			frame.AddChild(actor);
		}
	};
	void add_heads_from_layers(size_t column, std::vector<column_head>& heads,
		std::vector<NewSkinLayer>& layers);
	void set_column_info(size_t column, NewSkinColumn* newskin,
		NewSkinData& skin_data,
		const NoteData* note_data, const TimingData* timing_data, double x);

	void get_hold_draw_time(TapNote const& tap, double const hold_beat, double& beat, double& second);
	void draw_hold(QuantizedHoldRenderData& data, double head_beat,
		double head_second, double tail_beat, double tail_second);
	void update_displayed_beat(double beat, double second);
	bool y_offset_visible(double off)
	{
		return off >= first_y_offset_visible && off <= last_y_offset_visible;
	}
	bool note_visible(TapNote const& note, double const beat)
	{
		if(note.type == TapNoteType_HoldHead)
		{
			return y_offset_visible(calc_y_offset(beat, note.occurs_at_second)) ||
				y_offset_visible(calc_y_offset(beat + NoteRowToBeat(note.iDuration),
						note.end_second));
		}
		return y_offset_visible(calc_y_offset(beat, note.occurs_at_second));
	}
	double calc_y_offset(double beat, double second);
	double quantization_for_time(mod_val_inputs& input)
	{
		double mult= m_quantization_multiplier.evaluate(input);
		double offset= m_quantization_offset.evaluate(input);
		return fmodf((input.eval_beat * mult) + offset, 1.0);
	}
	void calc_transform(mod_val_inputs& input, transform& trans);
	void calc_reverse_shift();
	double apply_reverse_shift(double y_offset);

	enum render_step
	{
		RENDER_BELOW_NOTES,
		RENDER_HOLDS,
		RENDER_TAPS,
		RENDER_CHILDREN,
		RENDER_ABOVE_NOTES
	};
	void build_render_lists();
	void draw_things_in_step(render_step step);

	void pass_message_to_heads(Message& msg);
	void did_tap_note(TapNoteScore tns, bool bright);
	void did_hold_note(HoldNoteScore hns, bool bright);
	void set_hold_status(TapNote const* tap, bool start, bool end);
	void set_pressed(bool on);
	void set_note_upcoming(double beat_distance, double second_distance);

	virtual void UpdateInternal(float delta);
	virtual bool EarlyAbortDraw() const;
	void update_upcoming(double beat, double second);
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
		double upcoming_beat_dist;
		double upcoming_second_dist;
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

	ModifiableValue m_note_alpha;
	ModifiableValue m_note_glow;
	ModifiableValue m_receptor_alpha;
	ModifiableValue m_receptor_glow;
	ModifiableValue m_explosion_alpha;
	ModifiableValue m_explosion_glow;

private:
	double m_curr_beat;
	double m_curr_second;
	double m_pixels_visible_before_beat;
	double m_pixels_visible_after_beat;
	size_t m_column;
	NewSkinColumn* m_newskin;

	std::vector<column_head> m_heads_below_notes;
	std::vector<column_head> m_heads_above_notes;

	const NoteData* m_note_data;
	const TimingData* m_timing_data;
	// Data that needs to be stored for rendering below here.
	// Holds and taps are put into different lists because they have to be
	// rendered in different phases.  All hold bodies must be drawn first, then
	// all taps, so the taps appear on top of the hold bodies and are not
	// obscured.
	void draw_heads_internal(std::vector<column_head>& heads, bool receptors);
	void draw_holds_internal();
	void draw_taps_internal();
	NoteData::TrackMap::const_iterator first_note_visible_prev_frame;
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

	virtual void PushSelf(lua_State *L);
	virtual NewField* Copy() const;

	void push_columns_to_lua(lua_State* L);

	void clear_steps();
	void set_skin(RString const& skin_name);
	void set_steps(Steps* data);
	void set_note_data(NoteData* note_data, TimingData* timing, Style const* curr_style);

	void update_displayed_beat(double beat, double second);

	void did_tap_note(size_t column, TapNoteScore tns, bool bright);
	void did_hold_note(size_t column, HoldNoteScore hns, bool bright);
	void set_pressed(size_t column, bool on);

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
