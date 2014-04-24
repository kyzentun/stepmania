#include "global.h"
#include "NoteTypes.h"
#include "RageUtil.h"
#include "LuaManager.h"
#include "XmlFile.h"
#include "LocalizedString.h"

TapNote TAP_EMPTY			( TapNoteType_Empty,	TapNoteSubType_Invalid,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_TAP		( TapNoteType_Tap,		TapNoteSubType_Invalid,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_LIFT		( TapNoteType_Lift,	TapNoteSubType_Invalid,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_HOLD_HEAD		( TapNoteType_HoldHead,	TapNoteSubType_Hold,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_ROLL_HEAD		( TapNoteType_HoldHead,	TapNoteSubType_Roll,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_MINE		( TapNoteType_Mine,	TapNoteSubType_Invalid,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_ATTACK		( TapNoteType_Attack,	TapNoteSubType_Invalid,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_AUTO_KEYSOUND	( TapNoteType_AutoKeySound,TapNoteSubType_Invalid,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ORIGINAL_FAKE		( TapNoteType_Fake,	TapNoteSubType_Invalid,	TapNoteSource_Original, "", 0, -1 );
//TapNote TAP_ORIGINAL_MINE_HEAD	( TapNoteType_HoldHead,	TapNoteSubType_Mine,	TapNoteSource_Original, "", 0, -1 );
TapNote TAP_ADDITION_TAP		( TapNoteType_Tap,		TapNoteSubType_Invalid,	TapNoteSource_Addition, "", 0, -1 );
TapNote TAP_ADDITION_MINE		( TapNoteType_Mine,	TapNoteSubType_Invalid,	TapNoteSource_Addition, "", 0, -1 );

static const char *TapNoteTypeNames[] = {
	"Empty",
	"Tap",
	"HoldHead",
	"HoldTail",
	"Mine",
	"Lift",
	"Attack",
	"AutoKeySound",
	"Fake",
};
XToString( TapNoteType );
XToLocalizedString( TapNoteType );
LuaXType( TapNoteType );

static const char *TapNoteSubTypeNames[] = {
	"Hold",
	"Roll",
};
XToString( TapNoteSubType );
XToLocalizedString( TapNoteSubType );
LuaXType( TapNoteSubType );

static const char *TapNoteSourceNames[] = {
	"Original",
	"Addition",
};
XToString( TapNoteSource );
XToLocalizedString( TapNoteSource );
LuaXType( TapNoteSource );

static const char *NoteTypeNames[] = {
	"4th",
	"8th",
	"12th",
	"16th",
	"24th",
	"32nd",
	"48th",
	"64th",
	"192nd",
};
XToString( NoteType );
LuaXType( NoteType );
XToLocalizedString( NoteType );

/**
 * @brief Convert the NoteType to a beat representation.
 * @param nt the NoteType to check.
 * @return the proper beat.
 */
float NoteTypeToBeat( NoteType nt )
{
	switch( nt )
	{
	case NOTE_TYPE_4TH:	return 1.0f;	// quarter notes
	case NOTE_TYPE_8TH:	return 1.0f/2;	// eighth notes
	case NOTE_TYPE_12TH:	return 1.0f/3;	// quarter note triplets
	case NOTE_TYPE_16TH:	return 1.0f/4;	// sixteenth notes
	case NOTE_TYPE_24TH:	return 1.0f/6;	// eighth note triplets
	case NOTE_TYPE_32ND:	return 1.0f/8;	// thirty-second notes
	case NOTE_TYPE_48TH:	return 1.0f/12; // sixteenth note triplets
	case NOTE_TYPE_64TH:	return 1.0f/16; // sixty-fourth notes
	case NOTE_TYPE_192ND:	return 1.0f/48; // sixty-fourth note triplets
	case NoteType_Invalid:	return 1.0f/48;
	default:
		FAIL_M(ssprintf("Unrecognized note type: %i", nt));
	}
}

int NoteTypeToRow( NoteType nt )
{
	switch( nt )
	{
		case NOTE_TYPE_4TH: return 48;
		case NOTE_TYPE_8TH: return 24;
		case NOTE_TYPE_12TH: return 16;
		case NOTE_TYPE_16TH: return 12;
		case NOTE_TYPE_24TH: return 8;
		case NOTE_TYPE_32ND: return 6;
		case NOTE_TYPE_48TH: return 4;
		case NOTE_TYPE_64TH: return 3;
		case NOTE_TYPE_192ND:
		case NoteType_Invalid:
			return 1;
		default:
			FAIL_M("Invalid note type found: cannot convert to row.");
	}
}

/**
 * @brief The number of beats per measure.
 *
 * FIXME: Look at the time signature of the song and use that instead at some point. */
static const int BEATS_PER_MEASURE = 4;
/**
 * @brief The number of rows used in a measure.
 *
 * FIXME: Similar to the above, use time signatures and don't force hard-coded values. */
static const int ROWS_PER_MEASURE = ROWS_PER_BEAT * BEATS_PER_MEASURE;

/**
 * @brief Retrieve the proper quantized NoteType for the note.
 * @param row The row to check for.
 * @return the quantized NoteType. */
NoteType GetNoteType( int row )
{ 
	if(      row % (ROWS_PER_MEASURE/4) == 0)	return NOTE_TYPE_4TH;
	else if( row % (ROWS_PER_MEASURE/8) == 0)	return NOTE_TYPE_8TH;
	else if( row % (ROWS_PER_MEASURE/12) == 0)	return NOTE_TYPE_12TH;
	else if( row % (ROWS_PER_MEASURE/16) == 0)	return NOTE_TYPE_16TH;
	else if( row % (ROWS_PER_MEASURE/24) == 0)	return NOTE_TYPE_24TH;
	else if( row % (ROWS_PER_MEASURE/32) == 0)	return NOTE_TYPE_32ND;
	else if( row % (ROWS_PER_MEASURE/48) == 0)	return NOTE_TYPE_48TH;
	else if( row % (ROWS_PER_MEASURE/64) == 0)	return NOTE_TYPE_64TH;
	else						return NOTE_TYPE_192ND;
};

NoteType BeatToNoteType( float fBeat )
{ 
	return GetNoteType( BeatToNoteRow(fBeat) );
}
/**
 * @brief Determine if the row has a particular type of quantized note.
 * @param row the row in the Steps.
 * @param t the quantized NoteType to check for.
 * @return true if the NoteType is t, false otherwise.
 */
bool IsNoteOfType( int row, NoteType t )
{ 
	return GetNoteType(row) == t;
}


XNode* TapNoteResult::CreateNode() const
{
	XNode *p = new XNode( "TapNoteResult" );

	p->AppendAttr( "TapNoteScore", TapNoteScoreToString(tns) );
	p->AppendAttr( "TapNoteOffset", fTapNoteOffset );

	return p;
}

void TapNoteResult::LoadFromNode( const XNode* pNode )
{
	FAIL_M("TapNoteResult::LoadFromNode() is not implemented");
}

XNode* HoldNoteResult::CreateNode() const
{
	// XXX: Should this do anything?
	return new XNode( "HoldNoteResult" );
}

void HoldNoteResult::LoadFromNode( const XNode* pNode )
{
	FAIL_M("HoldNoteResult::LoadFromNode() is not implemented");
}

XNode* TapNote::CreateNode() const
{
	XNode *p = new XNode( "TapNote" );

	p->AppendChild( result.CreateNode() );
	p->AppendChild( HoldResult.CreateNode() );

	return p;
}

void TapNote::LoadFromNode( const XNode* pNode )
{
	FAIL_M("TapNote::LoadFromNode() is not implemented");
}

float HoldNoteResult::GetLastHeldBeat() const
{
	return NoteRowToBeat(iLastHeldRow);
}

// lua start
#include "LuaBinding.h"

void get_push_call(lua_State* L, int obj_index, char const* field_name)
{
	lua_getfield(L, obj_index, field_name);
	lua_pushvalue(L, obj_index);
	lua_call(L, 1, 1);
}

void TapNote::ConstructFromLuaInstance( lua_State *L, int stack_index)
{
	// This function exists to support the following lua snip:
	// local tapnote= notedata:GetTapNote(track, row)
	// notedata:SetTapNote(different_args, tapnote)
	Init();
	TapNote* src= Luna<TapNote>::check(L, stack_index);
	type= src->type;
	subType= src->subType;
	source= src->source;
	result= src->result;
	pn= src->pn;
	bHopoPossible= src->bHopoPossible;
	sAttackModifiers= src->sAttackModifiers.c_str();
	fAttackDurationSeconds= src->fAttackDurationSeconds;
	iKeysoundIndex= src->iKeysoundIndex;
	iDuration= src->iDuration;
	HoldResult= src->HoldResult;
}

void TapNote::ConstructFromLuaTable( lua_State *L, int stack_index)
{
	// This function exists to support the following lua snip:
	// notedata:SetTapNote(track, row, {Type= "TapNoteType_Tap"})
	// Any field of the table can be nil, and the default value is used.
	Init();
	lua_getfield(L, stack_index, "Type");
	type= Enum::Check<TapNoteType>(L, -1, true);
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "SubType");
	subType= Enum::Check<TapNoteSubType>(L, -1, true);
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "Source");
	source= Enum::Check<TapNoteSource>(L, -1, true);
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "PN");
	pn= Enum::Check<PlayerNumber>(L, -1, true);
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "Hopo");
	bHopoPossible= lua_toboolean(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "AttackMods");
	sAttackModifiers= luaL_optstring(L, -1, "");
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "AttackDuration");
	fAttackDurationSeconds= luaL_optnumber(L, -1, 0.0f);
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "KeySoundIndex");
	iKeysoundIndex= luaL_optint(L, -1, 0);
	lua_pop(L, 1);

	lua_getfield(L, stack_index, "Duration");
	iDuration= BeatToNoteRow(luaL_optnumber(L, -1, 0.0f));
	lua_pop(L, 1);
}

void TapNote::ConstructFromLuaState( lua_State *L, int stack_index)
{
	if(lua_type(L, stack_index) == LUA_TTABLE)
	{
		ConstructFromLuaTable(L, stack_index);
	}
	else if(lua_type(L, stack_index) == LUA_TUSERDATA)
	{
		ConstructFromLuaInstance(L, stack_index);
	}
	else
	{
		luaL_error(L, "SetTapNote passed unknown thing, TapNote expected.");
	}
}


/** @brief Allow Lua to have access to the NoteData. */
class LunaTapNote: public Luna<TapNote>
{
public:
	DEFINE_METHOD(GetType, type);
	DEFINE_METHOD(GetSubType, subType);
	DEFINE_METHOD(GetSource, source);
	DEFINE_METHOD(GetPN, pn);
	DEFINE_METHOD(GetHopo, bHopoPossible);
	DEFINE_METHOD(GetAttackMods, sAttackModifiers);
	DEFINE_METHOD(GetAttackDuration, fAttackDurationSeconds);
	DEFINE_METHOD(GetKeySoundIndex, iKeysoundIndex);
	DEFINE_METHOD(GetDurationInternal, iDuration);
	static int GetDuration( T* p, lua_State* L )
	{
		lua_pushnumber(L, NoteRowToBeat(p->iDuration));
		return 1;
	}
	static int SetType( T* p, lua_State* L )
	{
		p->type= Enum::Check<TapNoteType>(L, 1);
		return 0;
	}
	static int SetSubType( T* p, lua_State* L )
	{
		// TapNoteSubType_Invalid is actually the sub type of any tap note that isn't a hold of some kind.  Due to the way enums are passed to lua, TapNoteSubType_Invalid is pushed as nil.  It's reasonable for a theme to do something like "tapnote_a:SetSubType(tapnote_b:GetSubType())", so we have to accept nil as a valid argument.
		if(lua_isnil(L, 1))
		{
			p->subType= TapNoteSubType_Invalid;
		}
		else
		{
			p->subType= Enum::Check<TapNoteSubType>(L, 1);
		}
		return 0;
	}
	static int SetSource( T* p, lua_State* L )
	{
		p->source= Enum::Check<TapNoteSource>(L, 1);
		return 0;
	}
	static int SetPN( T* p, lua_State* L )
	{
		// Most tap notes seem to have PlayerNumber_Invalid set, so we have to consider nil to be a valid argument.  "tapnote_a:SetSubType(tapnote_b:GetSubType())" would not be valid if we did not accept nil.
		if(lua_isnil(L, 1))
		{
			p->pn= PlayerNumber_Invalid;
		}
		else
		{
			p->pn= Enum::Check<PlayerNumber>(L, 1);
		}
		return 0;
	}
	static int SetHopo( T* p, lua_State* L )
	{
		p->bHopoPossible= BArg(1);
		return 0;
	}
	static int SetAttackMods( T* p, lua_State* L )
	{
		p->sAttackModifiers= SArg(1);
		return 0;
	}
	static int SetAttackDuration( T* p, lua_State* L )
	{
		p->fAttackDurationSeconds= FArg(1);
		return 0;
	}
	static int SetKeySoundIndex( T* p, lua_State* L )
	{
		p->iKeysoundIndex= IArg(1);
		return 0;
	}
	static int SetDuration( T* p, lua_State* L )
	{
		p->iDuration= BeatToNoteRow(FArg(1));
		return 0;
	}

	LunaTapNote()
	{
		ADD_METHOD( GetType );
		ADD_METHOD( GetSubType );
		ADD_METHOD( GetSource );
		ADD_METHOD( GetPN );
		ADD_METHOD( GetHopo );
		ADD_METHOD( GetAttackMods );
		ADD_METHOD( GetAttackDuration );
		ADD_METHOD( GetKeySoundIndex );
		ADD_METHOD( GetDurationInternal );
		ADD_METHOD( GetDuration );
		ADD_METHOD( SetType );
		ADD_METHOD( SetSubType );
		ADD_METHOD( SetSource );
		ADD_METHOD( SetPN );
		ADD_METHOD( SetHopo );
		ADD_METHOD( SetAttackMods );
		ADD_METHOD( SetAttackDuration );
		ADD_METHOD( SetKeySoundIndex );
		ADD_METHOD( SetDuration );
	}
};

LUA_REGISTER_CLASS( TapNote )
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
