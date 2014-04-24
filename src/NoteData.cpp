/*
 * NoteData is organized by:
 *  track - corresponds to different columns of notes on the screen
 *  row/index - corresponds to subdivisions of beats
 */

#include "global.h"
#include "NoteData.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "XmlFile.h"
#include "GameState.h" // blame radar calculations.
#include "Foreach.h"
#include "RageUtil_AutoPtr.h"

REGISTER_CLASS_TRAITS( NoteData, new NoteData(*pCopy) )

void NoteData::Init()
{
	m_TapNotes = vector<TrackMap>();	// ensure that the memory is freed
}

void NoteData::SetNumTracks( int iNewNumTracks )
{
	ASSERT( iNewNumTracks > 0 );

	m_TapNotes.resize( iNewNumTracks );
}

bool NoteData::IsComposite() const
{
	for( int track = 0; track < GetNumTracks(); ++track )
	{
		FOREACHM_CONST( int, TapNote, m_TapNotes[track], tn )
			if( tn->second.pn != PLAYER_INVALID )
				return true;
	}
	return false;
}

// Clear (rowBegin,rowEnd).
void NoteData::ClearRangeForTrack( int rowBegin, int rowEnd, int iTrack )
{
	// Optimization: if the range encloses everything, just clear the whole maps.
	if( rowBegin == 0 && rowEnd == MAX_NOTE_ROW )
	{
		m_TapNotes[iTrack].clear();
		return;
	}

	/* If the range is empty, don't do anything. Otherwise, an empty range will
	 * cause hold notes to be split when they shouldn't be. */
	if( rowBegin == rowEnd )
		return;

	NoteData::TrackMap::iterator lBegin, lEnd;
	GetTapNoteRangeInclusive( iTrack, rowBegin, rowEnd, lBegin, lEnd );

	if( lBegin != lEnd && lBegin->first < rowBegin && lBegin->first + lBegin->second.iDuration > rowEnd )
	{
		/* A hold note overlaps the whole range. Truncate it, and add the
		 * remainder to the end. */
		TapNote tn1 = lBegin->second;
		TapNote tn2 = tn1;

		int iEndRow = lBegin->first + tn1.iDuration;
		int iRow = lBegin->first;

		tn1.iDuration = rowBegin - iRow;
		tn2.iDuration = iEndRow - rowEnd;

		SetTapNote( iTrack, iRow, tn1 );
		SetTapNote( iTrack, rowEnd, tn2 );

		// We may have invalidated our iterators.
		GetTapNoteRangeInclusive( iTrack, rowBegin, rowEnd, lBegin, lEnd );
	}
	else if( lBegin != lEnd && lBegin->first < rowBegin )
	{
		// A hold note overlaps the beginning of the range.  Truncate it.
		TapNote &tn1 = lBegin->second;
		int iRow = lBegin->first;
		tn1.iDuration = rowBegin - iRow;

		++lBegin;
	}

	if( lBegin != lEnd )
	{
		NoteData::TrackMap::iterator prev = lEnd;
		--prev;
		TapNote tn = lBegin->second;
		int iRow = prev->first;
		if( tn.type == TapNoteType_HoldHead && iRow + tn.iDuration > rowEnd )
		{
			// A hold note overlaps the end of the range.  Separate it.
			SetTapNote( iTrack, iRow, TAP_EMPTY );

			int iAdd = rowEnd - iRow;
			tn.iDuration -= iAdd;
			iRow += iAdd;
			SetTapNote( iTrack, iRow, tn );
			lEnd = prev;
		}

		// We may have invalidated our iterators.
		GetTapNoteRangeInclusive( iTrack, rowBegin, rowEnd, lBegin, lEnd );
	}

	m_TapNotes[iTrack].erase( lBegin, lEnd );
}

void NoteData::ClearRange( int rowBegin, int rowEnd )
{
	for( int t=0; t < GetNumTracks(); ++t )
		ClearRangeForTrack( rowBegin, rowEnd, t );
}

void NoteData::ClearAll()
{
	for( int t=0; t<GetNumTracks(); t++ )
		m_TapNotes[t].clear();
}

/* Copy [rowFromBegin,rowFromEnd) from pFrom to this. (Note that this does
 * *not* overlay; all data in the range is overwritten.) */
void NoteData::CopyRange( const NoteData& from, int rowFromBegin, int rowFromEnd, int rowToBegin )
{
	ASSERT( from.GetNumTracks() == GetNumTracks() );

	if( rowFromBegin > rowFromEnd )
		return; // empty range

	const int rowToEnd = (rowFromEnd-rowFromBegin) + rowToBegin;
	const int iMoveBy = rowToBegin-rowFromBegin;

	// Clear the region.
	ClearRange( rowToBegin, rowToEnd );

	for( int t=0; t<GetNumTracks(); t++ )
	{
		NoteData::TrackMap::const_iterator lBegin, lEnd;
		from.GetTapNoteRangeInclusive( t, rowFromBegin, rowFromEnd, lBegin, lEnd );
		for( ; lBegin != lEnd; ++lBegin )
		{
			TapNote head = lBegin->second;
			if( head.type == TapNoteType_Empty )
				continue;

			if( head.type == TapNoteType_HoldHead )
			{
				int iStartRow = lBegin->first + iMoveBy;
				int iEndRow = iStartRow + head.iDuration;

				iStartRow = clamp( iStartRow, rowToBegin, rowToEnd );
				iEndRow = clamp( iEndRow, rowToBegin, rowToEnd );

				this->AddHoldNote( t, iStartRow, iEndRow, head );
			}
			else
			{
				int iTo = lBegin->first + iMoveBy;
				if( iTo >= rowToBegin && iTo <= rowToEnd )
					this->SetTapNote( t, iTo, head );
			}
		}
	}
}

void NoteData::CopyAll( const NoteData& from )
{
	*this = from;
}

bool NoteData::IsRowEmpty( int row ) const
{
	for( int t=0; t<GetNumTracks(); t++ )
		if( GetTapNote(t, row).type != TapNoteType_Empty )
			return false;
	return true;
}

bool NoteData::IsRangeEmpty( int track, int rowBegin, int rowEnd ) const
{
	ASSERT( track < GetNumTracks() );

	FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, track, r, rowBegin, rowEnd )
		if( GetTapNote(track,r).type != TapNoteType_Empty )
			return false;
	return true;
}

int NoteData::GetNumTapNonEmptyTracks( int row ) const
{
	int iNum = 0;
	for( int t=0; t<GetNumTracks(); t++ )
		if( GetTapNote(t, row).type != TapNoteType_Empty )
			iNum++;
	return iNum;
}

void NoteData::GetTapNonEmptyTracks( int row, set<int>& addTo ) const
{
	for( int t=0; t<GetNumTracks(); t++ )
		if( GetTapNote(t, row).type != TapNoteType_Empty )
			addTo.insert(t);
}

bool NoteData::GetTapFirstNonEmptyTrack( int row, int &iNonEmptyTrackOut ) const
{
	for( int t=0; t<GetNumTracks(); t++ )
	{
		if( GetTapNote( t, row ).type != TapNoteType_Empty )
		{
			iNonEmptyTrackOut = t;
			return true;
		}
	}
	return false;
}

bool NoteData::GetTapFirstEmptyTrack( int row, int &iEmptyTrackOut ) const
{
	for( int t=0; t<GetNumTracks(); t++ )
	{
		if( GetTapNote( t, row ).type == TapNoteType_Empty )
		{
			iEmptyTrackOut = t;
			return true;
		}
	}
	return false;
}

bool NoteData::GetTapLastEmptyTrack( int row, int &iEmptyTrackOut ) const
{
	for( int t=GetNumTracks()-1; t>=0; t-- )
	{
		if( GetTapNote( t, row ).type == TapNoteType_Empty )
		{
			iEmptyTrackOut = t;
			return true;
		}
	}
	return false;
}

int NoteData::GetNumTracksWithTap( int row ) const
{
	int iNum = 0;
	for( int t=0; t<GetNumTracks(); t++ )
	{
		const TapNote &tn = GetTapNote( t, row );
		if( tn.type == TapNoteType_Tap || tn.type == TapNoteType_Lift )
			iNum++;
	}
	return iNum;
}

int NoteData::GetNumTracksWithTapOrHoldHead( int row ) const
{
	int iNum = 0;
	for( int t=0; t<GetNumTracks(); t++ )
	{
		const TapNote &tn = GetTapNote( t, row );
		if( tn.type == TapNoteType_Tap || tn.type == TapNoteType_Lift || tn.type == TapNoteType_HoldHead )
			iNum++;
	}
	return iNum;
}

int NoteData::GetFirstTrackWithTap( int row ) const
{
	for( int t=0; t<GetNumTracks(); t++ )
	{
		const TapNote &tn = GetTapNote( t, row );
		if( tn.type == TapNoteType_Tap || tn.type == TapNoteType_Lift )
			return t;
	}
	return -1;
}

int NoteData::GetFirstTrackWithTapOrHoldHead( int row ) const
{
	for( int t=0; t<GetNumTracks(); t++ )
	{
		const TapNote &tn = GetTapNote( t, row );
		if( tn.type == TapNoteType_Tap || tn.type == TapNoteType_Lift || tn.type == TapNoteType_HoldHead )
			return t;
	}
	return -1;
}

int NoteData::GetLastTrackWithTapOrHoldHead( int row ) const
{
	for( int t=GetNumTracks()-1; t>=0; t-- )
	{
		const TapNote &tn = GetTapNote( t, row );
		if( tn.type == TapNoteType_Tap || tn.type == TapNoteType_Lift || tn.type == TapNoteType_HoldHead )
			return t;
	}
	return -1;
}

int NoteData::GetNumTracksWithTapType( int row, TapNoteType type, bool invert ) const
{
	int ret= 0;
	for(int t= 0; t < GetNumTracks(); ++t)
	{
		TapNote const& tn= GetTapNote(t, row);
		// For the uninitiated, ^ is logical xor, and boolean true converts to 1.
		ret+= ((tn.type == type)^invert);
	}
	return ret;
}

int NoteData::GetNumTracksWithTapTypes( int row, vector<TapNoteType> const& types, bool invert ) const
{
	int ret= 0;
	for(int t= 0; t < GetNumTracks(); ++t)
	{
		TapNote const& tn= GetTapNote(t, row);
		bool matched= false;
		for(size_t c= 0; c < types.size(); ++c)
		{
			if(tn.type == types[c])
			{
				matched= true;
				break;
			}
		}
		// For the uninitiated, ^ is logical xor, and boolean true converts to 1.
		ret+= (matched^invert);
	}
	return ret;
}

void NoteData::GetTracksWithTapType(int row, TapNoteType type, vector<int>& out, bool invert) const
{
	for(int t= 0; t < GetNumTracks(); ++t)
	{
		TapNote const& tn= GetTapNote(t, row);
		if((tn.type == type)^invert)
		{
			out.push_back(t);
		}
	}
}

void NoteData::GetTracksWithTapTypes(int row, vector<TapNoteType> const& types, vector<int>& out, bool invert) const
{
	for(int t= 0; t < GetNumTracks(); ++t)
	{
		TapNote const& tn= GetTapNote(t, row);
		bool matched= false;
		for(size_t c= 0; c < types.size(); ++c)
		{
			if(tn.type == types[c])
			{
				matched= true;
				break;
			}
		}
		if(matched^invert)
		{
			out.push_back(t);
		}
	}
}

void NoteData::AddHoldNote( int iTrack, int iStartRow, int iEndRow, TapNote tn )
{
	ASSERT( iStartRow>=0 && iEndRow>=0 );
	ASSERT_M( iEndRow >= iStartRow, ssprintf("EndRow %d < StartRow %d",iEndRow,iStartRow) );
	// tn.iDuration is not used to calculate iEndRow because NoteDataUtil and ScreenEdit pass in TAP_ORIGINAL_HOLD_HEAD, which has an irrelevant duration. -Kyz

	/* Include adjacent (non-overlapping) hold notes, since we need to merge with them. */
	NoteData::TrackMap::iterator lBegin, lEnd;
	GetTapNoteRangeInclusive( iTrack, iStartRow, iEndRow, lBegin, lEnd, true );

	// Look for other hold notes that overlap and merge them into add.
	for( iterator it = lBegin; it != lEnd; ++it )
	{
		int iOtherRow = it->first;
		const TapNote &tnOther = it->second;
		if( tnOther.type == TapNoteType_HoldHead )
		{
			iStartRow = min( iStartRow, iOtherRow );
			iEndRow = max( iEndRow, iOtherRow + tnOther.iDuration );
		}
	}

	tn.iDuration = iEndRow - iStartRow;

	// Remove everything in the range.
	while( lBegin != lEnd )
	{
		iterator next = lBegin;
		++next;

		RemoveTapNote( iTrack, lBegin );

		lBegin = next;
	}

	/* Additionally, if there's a tap note lying at the end of our range,
	 * remove it too. */
	SetTapNote( iTrack, iEndRow, TAP_EMPTY );

	// add a tap note at the start of this hold
	SetTapNote( iTrack, iStartRow, tn );
}

/* Determine if a hold note lies on the given spot.  Return true if so.  If
 * pHeadRow is non-NULL, return the row of the head. */
bool NoteData::IsHoldHeadOrBodyAtRow( int iTrack, int iRow, int *pHeadRow ) const
{
	const TapNote &tn = GetTapNote( iTrack, iRow );
	if( tn.type == TapNoteType_HoldHead )
	{
		if( pHeadRow != NULL )
			*pHeadRow = iRow;
		return true;
	}

	return IsHoldNoteAtRow( iTrack, iRow, pHeadRow );
}

/* Determine if a hold note lies on the given spot. Return true if so.  If
 * pHeadRow is non-NULL, return the row of the head. (Note that this returns
 * false if a hold head lies on iRow itself.) */
/* XXX: rename this to IsHoldBodyAtRow */
bool NoteData::IsHoldNoteAtRow( int iTrack, int iRow, int *pHeadRow ) const
{
	int iDummy;
	if( pHeadRow == NULL )
		pHeadRow = &iDummy;

	/* Starting at iRow, search upwards. If we find a TapNoteType_HoldHead, we're within
	 * a hold. If we find a tap, mine or attack, we're not--those never lie
	 * within hold notes. Ignore autoKeysound. */
	FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE_REVERSE( *this, iTrack, r, 0, iRow )
	{
		const TapNote &tn = GetTapNote( iTrack, r );
		switch( tn.type )
		{
		case TapNoteType_HoldHead:
			if( tn.iDuration + r < iRow )
				return false;
			*pHeadRow = r;
			return true;

		case TapNoteType_Tap:
		case TapNoteType_Mine:
		case TapNoteType_Attack:
		case TapNoteType_Lift:
		case TapNoteType_Fake:
			return false;

		case TapNoteType_Empty:
		case TapNoteType_AutoKeySound:
			// ignore
			continue;
		DEFAULT_FAIL( tn.type );
		}
	}

	return false;
}

bool NoteData::IsEmpty() const
{ 
	for( int t=0; t < GetNumTracks(); t++ )
	{
		int iRow = -1;
		if( !GetNextTapNoteRowForTrack( t, iRow ) )
			continue;

		return false;
	}

	return true;
}

int NoteData::GetFirstRow() const
{ 
	int iEarliestRowFoundSoFar = -1;

	for( int t=0; t < GetNumTracks(); t++ )
	{
		int iRow = -1;
		if( !GetNextTapNoteRowForTrack( t, iRow ) )
			continue;

		if( iEarliestRowFoundSoFar == -1 )
			iEarliestRowFoundSoFar = iRow;
		else
			iEarliestRowFoundSoFar = min( iEarliestRowFoundSoFar, iRow );
	}

	if( iEarliestRowFoundSoFar == -1 )	// there are no notes
		return 0;

	return iEarliestRowFoundSoFar;
}

int NoteData::GetLastRow() const
{ 
	int iOldestRowFoundSoFar = 0;

	for( int t=0; t < GetNumTracks(); t++ )
	{
		int iRow = MAX_NOTE_ROW;
		if( !GetPrevTapNoteRowForTrack( t, iRow ) )
			continue;

		/* XXX: We might have a hold note near the end with autoplay sounds
		 * after it.  Do something else with autoplay sounds ... */
		const TapNote &tn = GetTapNote( t, iRow );
		if( tn.type == TapNoteType_HoldHead )
			iRow += tn.iDuration;

		iOldestRowFoundSoFar = max( iOldestRowFoundSoFar, iRow );
	}

	return iOldestRowFoundSoFar;
}

bool NoteData::IsTap(const TapNote &tn, const int row) const
{
	return (tn.type != TapNoteType_Empty && tn.type != TapNoteType_Mine
			&& tn.type != TapNoteType_Lift && tn.type != TapNoteType_Fake
			&& tn.type != TapNoteType_AutoKeySound
			&& GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

bool NoteData::IsMine(const TapNote &tn, const int row) const
{
	return (tn.type == TapNoteType_Mine
			&& GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

bool NoteData::IsLift(const TapNote &tn, const int row) const
{
	return (tn.type == TapNoteType_Lift
			&& GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

bool NoteData::IsFake(const TapNote &tn, const int row) const
{
	return (tn.type == TapNoteType_Fake
			|| !GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

int NoteData::GetNumTapNotes( int iStartIndex, int iEndIndex ) const
{
	int iNumNotes = 0;
	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
		{
			if (this->IsTap(GetTapNote(t, r), r))
				iNumNotes++;
		}
	}

	return iNumNotes;
}

int NoteData::GetNumTapNotesInRow( int iRow ) const
{
	int iNumNotes = 0;
	for( int t=0; t<GetNumTracks(); t++ )
	{
		if (this->IsTap(GetTapNote(t, iRow), iRow))
			iNumNotes++;
	}
	return iNumNotes;
}

int NoteData::GetNumRowsWithTap( int iStartIndex, int iEndIndex ) const
{
	int iNumNotes = 0;
	FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE( *this, r, iStartIndex, iEndIndex )
		if( IsThereATapAtRow(r) && GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(r) )
			iNumNotes++;

	return iNumNotes;
}

int NoteData::GetNumMines( int iStartIndex, int iEndIndex ) const
{
	int iNumMines = 0;

	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
			if (this->IsMine(GetTapNote(t, r), r))
				iNumMines++;
	}

	return iNumMines;
}

int NoteData::GetNumRowsWithTapOrHoldHead( int iStartIndex, int iEndIndex ) const
{
	int iNumNotes = 0;
	FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE( *this, r, iStartIndex, iEndIndex )
		if( IsThereATapOrHoldHeadAtRow(r) && GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(r) )
			iNumNotes++;

	return iNumNotes;
}

bool NoteData::RowNeedsAtLeastSimultaneousPresses( int iMinSimultaneousPresses, const int row ) const
{
	int iNumNotesThisIndex = 0;
	for( int t=0; t<GetNumTracks(); t++ )
	{
		const TapNote &tn = GetTapNote(t, row);
		switch( tn.type )
		{
			case TapNoteType_Mine:
			case TapNoteType_Empty:
			case TapNoteType_Fake:
			case TapNoteType_Lift: // you don't "press" on a lift.
			case TapNoteType_AutoKeySound:
				continue;	// skip these types - they don't count
			default: break;
		}
		++iNumNotesThisIndex;
	}

	/* We must have at least one tap or hold head at this row to count it. */
	if( !iNumNotesThisIndex )
		return false;

	if( iNumNotesThisIndex < iMinSimultaneousPresses )
	{
		/* We have at least one, but not enough.  Count holds.  Do count adjacent holds. */
		for( int t=0; t<GetNumTracks(); ++t )
		{
			if( IsHoldNoteAtRow(t, row) )
				++iNumNotesThisIndex;
		}
	}

	return iNumNotesThisIndex >= iMinSimultaneousPresses;
}

int NoteData::GetNumRowsWithSimultaneousPresses( int iMinSimultaneousPresses, int iStartIndex, int iEndIndex ) const
{
	/* Count the number of times you have to use your hands.  This includes
	 * three taps at the same time, a tap while two hold notes are being held,
	 * etc.  Only count rows that have at least one tap note (hold heads count).
	 * Otherwise, every row of hold notes counts, so three simultaneous hold
	 * notes will count as hundreds of "hands". */
	int iNum = 0;
	FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE( *this, r, iStartIndex, iEndIndex )
	{
		if( !RowNeedsAtLeastSimultaneousPresses(iMinSimultaneousPresses,r) )
			continue;
		if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(r))
			continue;
		iNum++;
	}

	return iNum;
}

int NoteData::GetNumRowsWithSimultaneousTaps( int iMinTaps, int iStartIndex, int iEndIndex ) const
{
	int iNum = 0;
	FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE( *this, r, iStartIndex, iEndIndex )
	{
		if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(r))
			continue;
		int iNumNotesThisIndex = 0;
		for( int t=0; t<GetNumTracks(); t++ )
		{
			const TapNote &tn = GetTapNote(t, r);
			if (tn.type != TapNoteType_Mine &&     // mines don't count.
				tn.type != TapNoteType_Empty &&
				tn.type != TapNoteType_Fake &&
				tn.type != TapNoteType_AutoKeySound)
				iNumNotesThisIndex++;
		}
		if( iNumNotesThisIndex >= iMinTaps )
			iNum++;
	}

	return iNum;
}

int NoteData::GetNumHoldNotes( int iStartIndex, int iEndIndex ) const
{
	int iNumHolds = 0;
	for( int t=0; t<GetNumTracks(); ++t )
	{
		NoteData::TrackMap::const_iterator lBegin, lEnd;
		GetTapNoteRangeExclusive( t, iStartIndex, iEndIndex, lBegin, lEnd );
		for( ; lBegin != lEnd; ++lBegin )
		{
			if( lBegin->second.type != TapNoteType_HoldHead ||
				lBegin->second.subType != TapNoteSubType_Hold )
				continue;
			if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(lBegin->first))
				continue;
			iNumHolds++;
		}
	}
	return iNumHolds;
}

int NoteData::GetNumRolls( int iStartIndex, int iEndIndex ) const
{
	int iNumRolls = 0;
	for( int t=0; t<GetNumTracks(); ++t )
	{
		NoteData::TrackMap::const_iterator lBegin, lEnd;
		GetTapNoteRangeExclusive( t, iStartIndex, iEndIndex, lBegin, lEnd );
		for( ; lBegin != lEnd; ++lBegin )
		{
			if( lBegin->second.type != TapNoteType_HoldHead ||
				lBegin->second.subType != TapNoteSubType_Roll )
				continue;
			if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(lBegin->first))
				continue;
			iNumRolls++;
		}
	}
	return iNumRolls;
}

int NoteData::GetNumLifts( int iStartIndex, int iEndIndex ) const
{
	int iNumLifts = 0;

	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
			if( this->IsLift(GetTapNote(t, r), r))
				iNumLifts++;
	}

	return iNumLifts;
}

int NoteData::GetNumFakes( int iStartIndex, int iEndIndex ) const
{
	int iNumFakes = 0;
	
	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
			if( this->IsFake(GetTapNote(t, r), r))
				iNumFakes++;
	}
	
	return iNumFakes;
}

bool NoteData::IsPlayer1(const int track, const TapNote &tn) const
{
	if (this->IsComposite())
	{
		return tn.pn == PLAYER_1;
	}
	return track < (this->GetNumTracks() / 2);
}

pair<int, int> NoteData::GetNumTapNotesTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	pair<int, int> num(0, 0);
	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
		{
			const TapNote &tn = GetTapNote(t, r);
			if (this->IsTap(tn, r))
			{
				if (this->IsPlayer1(t, tn))
					num.first++;
				else
					num.second++;
			}
		}
	}
	return num;
}

pair<int, int> NoteData::GetNumRowsWithSimultaneousTapsTwoPlayer(int minTaps,
																 int startRow,
																 int endRow) const
{
	pair<int, int> num(0, 0);
	FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE( *this, r, startRow, endRow )
	{
		pair<int, int> found(0, 0);
		for( int t=0; t<GetNumTracks(); t++ )
		{
			const TapNote &tn = GetTapNote(t, r);
			if (this->IsTap(tn, r))
			{
				if (this->IsPlayer1(t, tn))
					found.first++;
				else
					found.second++;
			}
		}
		if (found.first >= minTaps)
			num.first++;
		if (found.second >= minTaps)
			num.second++;
	}
	return num;
}

pair<int, int> NoteData::GetNumJumpsTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	return GetNumRowsWithSimultaneousTapsTwoPlayer( 2, iStartIndex, iEndIndex );
}

pair<int, int> NoteData::GetNumHandsTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	return GetNumRowsWithSimultaneousTapsTwoPlayer( 3, iStartIndex, iEndIndex );
}

pair<int, int> NoteData::GetNumQuadsTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	return GetNumRowsWithSimultaneousTapsTwoPlayer( 4, iStartIndex, iEndIndex );
}

pair<int, int> NoteData::GetNumHoldNotesTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	pair<int, int> num(0, 0);
	for( int t=0; t<GetNumTracks(); ++t )
	{
		NoteData::TrackMap::const_iterator lBegin, lEnd;
		GetTapNoteRangeExclusive( t, iStartIndex, iEndIndex, lBegin, lEnd );
		for( ; lBegin != lEnd; ++lBegin )
		{
			if( lBegin->second.type != TapNoteType_HoldHead ||
			   lBegin->second.subType != TapNoteSubType_Hold )
				continue;
			if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(lBegin->first))
				continue;
			if (this->IsPlayer1(t, lBegin->second))
				num.first++;
			else
				num.second++;
		}
	}
	return num;
}

pair<int, int> NoteData::GetNumMinesTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	pair<int, int> num(0, 0);
	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
		{
			const TapNote &tn = GetTapNote(t, r);
			if (this->IsMine(tn, r))
			{
				if (this->IsPlayer1(t, tn))
					num.first++;
				else
					num.second++;
			}
		}
	}
	return num;
}

pair<int, int> NoteData::GetNumRollsTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	pair<int, int> num(0, 0);
	for( int t=0; t<GetNumTracks(); ++t )
	{
		NoteData::TrackMap::const_iterator lBegin, lEnd;
		GetTapNoteRangeExclusive( t, iStartIndex, iEndIndex, lBegin, lEnd );
		for( ; lBegin != lEnd; ++lBegin )
		{
			if( lBegin->second.type != TapNoteType_HoldHead ||
			   lBegin->second.subType != TapNoteSubType_Roll )
				continue;
			if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(lBegin->first))
				continue;
			if (this->IsPlayer1(t, lBegin->second))
				num.first++;
			else
				num.second++;
		}
	}
	return num;
}

pair<int, int> NoteData::GetNumLiftsTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	pair<int, int> num(0, 0);
	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
		{
			const TapNote &tn = GetTapNote(t, r);
			if (this->IsLift(tn, r))
			{
				if (this->IsPlayer1(t, tn))
					num.first++;
				else
					num.second++;
			}
		}
	}
	return num;
}

pair<int, int> NoteData::GetNumFakesTwoPlayer( int iStartIndex, int iEndIndex ) const
{
	pair<int, int> num(0, 0);
	for( int t=0; t<GetNumTracks(); t++ )
	{
		FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE( *this, t, r, iStartIndex, iEndIndex )
		{
			const TapNote &tn = GetTapNote(t, r);
			if (this->IsFake(tn, r))
			{
				if (this->IsPlayer1(t, tn))
					num.first++;
				else
					num.second++;
			}
		}
	}
	return num;
}

/*
int NoteData::GetNumMinefields( int iStartIndex, int iEndIndex ) const
{
	int iNumMinefields = 0;
	for( int t=0; t<GetNumTracks(); ++t )
	{
		NoteData::TrackMap::const_iterator begin, end;
		GetTapNoteRangeExclusive( t, iStartIndex, iEndIndex, begin, end );
		for( ; begin != end; ++begin )
		{
			if( begin->second.type != TapNoteType_HoldHead ||
				begin->second.subType != TapNoteSubType_Mine )
				continue;
			iNumMinefields++;
		}
	}
	return iNumMinefields;
}
*/

// -1 for iOriginalTracksToTakeFrom means no track
void NoteData::LoadTransformed( const NoteData& in, int iNewNumTracks, vector<int> const& iOriginalTrackToTakeFrom )
{
	ASSERT_M( (size_t)iNewNumTracks <= iOriginalTrackToTakeFrom.size(),
		ssprintf("bad track mapping"));
	// reset all notes
	Init();

	SetNumTracks( iNewNumTracks );

	// copy tracks
	for( int t=0; t<GetNumTracks(); t++ )
	{
		const int iOriginalTrack = iOriginalTrackToTakeFrom[t];
		ASSERT_M( iOriginalTrack < in.GetNumTracks(), ssprintf("from OriginalTrack %i >= %i (#tracks) (taking from %i)", 
			iOriginalTrack, in.GetNumTracks(), iOriginalTrackToTakeFrom[t]));

		if( iOriginalTrack == -1 )
			continue;
		m_TapNotes[t] = in.m_TapNotes[iOriginalTrack];
	}
}

void NoteData::MoveTapNoteTrack( int dest, int src )
{
	if(dest == src) return;
	m_TapNotes[dest] = m_TapNotes[src];
	m_TapNotes[src].clear();
}

void NoteData::SetTapNote( int track, int row, const TapNote& t )
{
	DEBUG_ASSERT( track>=0 && track<GetNumTracks() );

	if( row < 0 )
		return;

	// There's no point in inserting empty notes into the map.
	// Any blank space in the map is defined to be empty.
	// If we're trying to insert an empty at a spot where another note
	// already exists, then we're really deleting from the map.
	if( t == TAP_EMPTY )
	{
		TrackMap &trackMap = m_TapNotes[track];
		// remove the element at this position (if any).
		// This will return either 0 or 1.
		trackMap.erase( row );
	}
	else
	{
		m_TapNotes[track][row] = t;
	}
}

void NoteData::GetTracksHeldAtRow( int row, set<int>& addTo )
{
	for( int t=0; t<GetNumTracks(); ++t )
		if( IsHoldNoteAtRow( t, row ) )
			addTo.insert( t );
}

int NoteData::GetNumTracksHeldAtRow( int row )
{
	static set<int> viTracks;
	viTracks.clear();
	GetTracksHeldAtRow( row, viTracks );
	return viTracks.size();
}

bool NoteData::GetNextTapNoteRowForTrack( int track, int &rowInOut ) const
{
	const TrackMap &mapTrack = m_TapNotes[track];

	// lower_bound and upper_bound have the same effect here because duplicate 
	// keys aren't allowed.

	// lower_bound "finds the first element whose key is not less than k" (>=);
	// upper_bound "finds the first element whose key greater than k".  They don't
	// have the same effect, but lower_bound(row+1) should equal upper_bound(row). -glenn
	TrackMap::const_iterator iter = mapTrack.lower_bound( rowInOut+1 );	// "find the first note for which row+1 < key == false"
	if( iter == mapTrack.end() )
		return false;

	ASSERT( iter->first > rowInOut );
	rowInOut = iter->first;
	return true;
}

bool NoteData::GetPrevTapNoteRowForTrack( int track, int &rowInOut ) const
{
	const TrackMap &mapTrack = m_TapNotes[track];

	// Find the first note >= rowInOut.
	TrackMap::const_iterator iter = mapTrack.lower_bound( rowInOut );

	// If we're at the beginning, we can't move back any more.
	if( iter == mapTrack.begin() )
		return false;

	// Move back by one.
	--iter;	
	ASSERT( iter->first < rowInOut );
	rowInOut = iter->first;
	return true;
}

NoteData::iterator NoteData::row_to_iterator(int track, int row)
{
	TrackMap& map_track= m_TapNotes[track];
	if(row <= 0)
	{
		return map_track.begin();
	}
	if(row >= MAX_NOTE_ROW)
	{
		return map_track.end();
	}
	return map_track.lower_bound(row);
}

NoteData::reverse_iterator NoteData::row_to_reverse_iterator(int track, int row)
{
	TrackMap& map_track= m_TapNotes[track];
	if(row <= 0)
	{
		return map_track.rend();
	}
	if(row >= MAX_NOTE_ROW)
	{
		return map_track.rbegin();
	}
	return TrackMap::reverse_iterator(map_track.lower_bound(row));
}

void NoteData::GetTapNoteRange( int iTrack, int iStartRow, int iEndRow, TrackMap::iterator &lBegin, TrackMap::iterator &lEnd )
{
	ASSERT_M( iTrack < GetNumTracks(), ssprintf("%i,%i", iTrack, GetNumTracks())  );
	TrackMap &mapTrack = m_TapNotes[iTrack];

	if( iStartRow > iEndRow )
	{
		lBegin = lEnd = mapTrack.end();
		return;
	}
	lBegin= row_to_iterator(iTrack, iStartRow);
	lEnd= row_to_iterator(iTrack, iEndRow);
}

void NoteData::GetTapNoteRangeReverse( int iTrack, int iStartRow, int iEndRow, TrackMap::reverse_iterator &rlBegin, TrackMap::reverse_iterator &rlEnd )
{
	ASSERT_M( iTrack < GetNumTracks(), ssprintf("%i,%i", iTrack, GetNumTracks()));
	TrackMap& mapTrack= m_TapNotes[iTrack];
	if(iStartRow < iEndRow)
	{
		rlBegin = rlEnd = mapTrack.rend();
		return;
	}
	rlBegin= row_to_reverse_iterator(iTrack, iStartRow);
	rlEnd= row_to_reverse_iterator(iTrack, iEndRow);
}

/* Include hold notes that overlap the edges.  If a hold note completely surrounds the given
 * range, included it, too.  If bIncludeAdjacent is true, also include hold notes adjacent to,
 * but not overlapping, the edge. */
void NoteData::GetTapNoteRangeInclusive( int iTrack, int iStartRow, int iEndRow, TrackMap::iterator &lBegin, TrackMap::iterator &lEnd, bool bIncludeAdjacent )
{
	GetTapNoteRange( iTrack, iStartRow, iEndRow, lBegin, lEnd );

	if( lBegin != this->begin(iTrack) )
	{
		iterator prev = Decrement(lBegin);

		const TapNote &tn = prev->second;
		if( tn.type == TapNoteType_HoldHead )
		{
			int iHoldStartRow = prev->first;
			int iHoldEndRow = iHoldStartRow + tn.iDuration;
			if( bIncludeAdjacent )
				++iHoldEndRow;
			if( iHoldEndRow > iStartRow )
			{
				// The previous note is a hold.
				lBegin = prev;
			}
		}
	}

	if( bIncludeAdjacent && lEnd != this->end(iTrack) )
	{
		// Include the next note if it's a hold and starts on iEndRow.
		const TapNote &tn = lEnd->second;
		int iHoldStartRow = lEnd->first;
		if( tn.type == TapNoteType_HoldHead && iHoldStartRow == iEndRow )
			++lEnd;
	}
}

void NoteData::GetTapNoteRangeExclusive( int iTrack, int iStartRow, int iEndRow, TrackMap::iterator &lBegin, TrackMap::iterator &lEnd )
{
	GetTapNoteRange( iTrack, iStartRow, iEndRow, lBegin, lEnd );

	// If end-1 is a hold_head, and extends beyond iEndRow, exclude it.
	if( lBegin != lEnd && lEnd != this->begin(iTrack) )
	{
		iterator prev = lEnd;
		--prev;
		if( prev->second.type == TapNoteType_HoldHead )
		{
			int localStartRow = prev->first;
			const TapNote &tn = prev->second;
			if( localStartRow + tn.iDuration >= iEndRow )
				lEnd = prev;
		}
	}
}

void NoteData::GetTapNoteRange( int iTrack, int iStartRow, int iEndRow, TrackMap::const_iterator &lBegin, TrackMap::const_iterator &lEnd ) const
{
	TrackMap::iterator const_begin, const_end;
	const_cast<NoteData *>(this)->GetTapNoteRange( iTrack, iStartRow, iEndRow, const_begin, const_end );
	lBegin = const_begin;
	lEnd = const_end;
}

void NoteData::GetTapNoteRangeInclusive( int iTrack, int iStartRow, int iEndRow, TrackMap::const_iterator &lBegin, TrackMap::const_iterator &lEnd, bool bIncludeAdjacent ) const
{
	TrackMap::iterator const_begin, const_end;
	const_cast<NoteData *>(this)->GetTapNoteRangeInclusive( iTrack, iStartRow, iEndRow, const_begin, const_end, bIncludeAdjacent );
	lBegin = const_begin;
	lEnd = const_end;
}

void NoteData::GetTapNoteRangeExclusive( int iTrack, int iStartRow, int iEndRow, TrackMap::const_iterator &lBegin, TrackMap::const_iterator &lEnd ) const
{
	TrackMap::iterator const_begin, const_end;
	const_cast<NoteData *>(this)->GetTapNoteRange( iTrack, iStartRow, iEndRow, const_begin, const_end );
	lBegin = const_begin;
	lEnd = const_end;
}



bool NoteData::GetNextTapNoteRowForAllTracks( int &rowInOut ) const
{
	int iClosestNextRow = MAX_NOTE_ROW;
	bool bAnyHaveNextNote = false;
	for( int t=0; t<GetNumTracks(); t++ )
	{
		int iNewRowThisTrack = rowInOut;
		if( GetNextTapNoteRowForTrack( t, iNewRowThisTrack ) )
		{
			bAnyHaveNextNote = true;
			ASSERT( iNewRowThisTrack < MAX_NOTE_ROW );
			iClosestNextRow = min( iClosestNextRow, iNewRowThisTrack );
		}
	}

	if( bAnyHaveNextNote )
	{
		rowInOut = iClosestNextRow;
		return true;
	}
	else
	{
		return false;
	}
}

bool NoteData::GetPrevTapNoteRowForAllTracks( int &rowInOut ) const
{
	int iClosestPrevRow = 0;
	bool bAnyHavePrevNote = false;
	for( int t=0; t<GetNumTracks(); t++ )
	{
		int iNewRowThisTrack = rowInOut;
		if( GetPrevTapNoteRowForTrack( t, iNewRowThisTrack ) )
		{
			bAnyHavePrevNote = true;
			ASSERT( iNewRowThisTrack < MAX_NOTE_ROW );
			iClosestPrevRow = max( iClosestPrevRow, iNewRowThisTrack );
		}
	}

	if( bAnyHavePrevNote )
	{
		rowInOut = iClosestPrevRow;
		return true;
	}
	else
	{
		return false;
	}
}

XNode* NoteData::CreateNode() const
{
	XNode *p = new XNode( "NoteData" );

	all_tracks_const_iterator iter = GetTapNoteRangeAllTracks( 0, GetLastRow() );

	for( ; !iter.IsAtEnd(); ++iter )
	{
		XNode *p2 = iter->CreateNode();

		p2->AppendAttr( "Track", iter.Track() );
		p2->AppendAttr( "Row", iter.Row() );
		p->AppendChild( p2 );
	}
	return p;
}

void NoteData::LoadFromNode( const XNode* pNode )
{
	FAIL_M("NoteData::LoadFromNode() not implemented");
}

template<typename ND, typename iter, typename TN>
void NoteData::_all_tracks_iterator<ND, iter, TN>::Find( bool bReverse )
{
	// If no notes can be found in the range, m_iTrack will stay -1 and IsAtEnd() will return true.
	m_iTrack = -1;
	if( bReverse )
	{
		int iMaxRow = INT_MIN;
		for( int iTrack = m_pNoteData->GetNumTracks() - 1; iTrack >= 0; --iTrack )
		{
			iter &i( m_vCurrentIters[iTrack] );
			const iter &end = m_vEndIters[iTrack];
			if( i != end  &&  i->first > iMaxRow )
			{
				iMaxRow = i->first;
				m_iTrack = iTrack;
			}
		}
	}
	else
	{

		int iMinRow = INT_MAX;
		for( int iTrack = 0; iTrack < m_pNoteData->GetNumTracks(); ++iTrack )
		{
			iter &i = m_vCurrentIters[iTrack];
			const iter &end = m_vEndIters[iTrack];
			if( i != end  &&  i->first < iMinRow )
			{
				iMinRow = i->first;
				m_iTrack = iTrack;
			}
		}
	}
}

template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>::_all_tracks_iterator( ND &nd, int iStartRow, int iEndRow, bool bReverse, bool bInclusive ) :
	m_pNoteData(&nd), m_iTrack(0), m_bReverse(bReverse)
{
	ASSERT( m_pNoteData->GetNumTracks() > 0 );

	for( int iTrack = 0; iTrack < m_pNoteData->GetNumTracks(); ++iTrack )
	{
		iter begin, end;
		if( bInclusive )
			m_pNoteData->GetTapNoteRangeInclusive( iTrack, iStartRow, iEndRow, begin, end );
		else
			m_pNoteData->GetTapNoteRange( iTrack, iStartRow, iEndRow, begin, end );

		m_vBeginIters.push_back( begin );
		m_vEndIters.push_back( end );

		iter cur;
		if( m_bReverse )
		{
			cur = end;
			if( cur != begin )
				cur--;
		}
		else
		{
			cur = begin;
		}
		m_vCurrentIters.push_back( cur );
	}

	Find( bReverse );
}

template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>::_all_tracks_iterator( const _all_tracks_iterator &other ) :
#define COPY_OTHER( x ) x( other.x )
	COPY_OTHER( m_pNoteData ),
	COPY_OTHER( m_vBeginIters ),
	COPY_OTHER( m_vCurrentIters ),
	COPY_OTHER( m_vEndIters ),
	COPY_OTHER( m_iTrack ),
	COPY_OTHER( m_bReverse )
#undef COPY_OTHER
{
}

template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN> &NoteData::_all_tracks_iterator<ND, iter, TN>::operator++() // preincrement
{
	if( m_bReverse )
	{
		if( m_vCurrentIters[m_iTrack] == m_vBeginIters[m_iTrack] )
			m_vCurrentIters[m_iTrack] = m_vEndIters[m_iTrack];
		else
			--m_vCurrentIters[m_iTrack];
	}
	else
	{
		++m_vCurrentIters[m_iTrack];
	}
	Find( m_bReverse );
	return *this;
}

template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN> NoteData::_all_tracks_iterator<ND, iter, TN>::operator++( int ) // postincrement
{
	_all_tracks_iterator<ND, iter, TN> ret( *this );
	operator++();
	return ret;
}
/* XXX: This doesn't satisfy the requirements that ++iter; --iter; is a no-op so it cannot be bidirectional for now. */
#if 0
template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN> &NoteData::_all_tracks_iterator<ND, iter, TN>::operator--() // predecrement
{
	if( m_bReverse )
	{
		++m_vCurrentIters[m_iTrack];
	}
	else
	{
		if( m_vCurrentIters[m_iTrack] == m_vEndIters[m_iTrack] )
			m_vCurrentIters[m_iTrack] = m_vEndIters[m_iTrack];
		else
			--m_vCurrentIters[m_iTrack];
	}
	Find( !m_bReverse );
	return *this;
}

template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN> NoteData::_all_tracks_iterator<ND, iter, TN>::operator--( int dummy ) // postdecrement
{
	_all_tracks_iterator<ND, iter, TN> ret( *this );
	operator--();
	return ret;
}
#endif

// Explicit instantiation.
template class NoteData::_all_tracks_iterator<NoteData, NoteData::iterator, TapNote>;
template class NoteData::_all_tracks_iterator<const NoteData, NoteData::const_iterator, const TapNote>;

// lua start
#include "LuaBinding.h"
// A little wrapper to allow beats on the lua side to use "-1" to mean MAX_NOTE_ROW.
int LuaBeatToNoteRow(float beat); // prototype to silence compiler warnings.
int LuaBeatToNoteRow(float beat)
{
	if(beat < 0.0f)
	{
		return MAX_NOTE_ROW;
	}
	return BeatToNoteRow(beat);
}

/** @brief Allow Lua to have access to the NoteData. */
class LunaNoteData: public Luna<NoteData>
{
public:
	static unsigned int CheckedTrackNumber( T* p, lua_State* L, int TrackNumberIndex)
	{
		// lua indices are one-indexed.
		unsigned int track= IArg(TrackNumberIndex);
		unsigned int num_tracks= p->GetNumTracks();
		if(track >= num_tracks)
		{
			LuaHelpers::Push( L, ssprintf("Track number greater than the number of tracks.  %u > %u", track, num_tracks));
			lua_error( L );
		}
		return track;
	}
	DEFINE_METHOD( GetNumTracks, GetNumTracks() );
	DEFINE_METHOD( IsComposite, IsComposite() );
	static int GetTapNote( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int row= BeatToNoteRow(FArg(2));
		NoteData::iterator found_note= p->FindTapNote(track, row);
		if(found_note != p->GetTapsEnd(track))
		{
			found_note->second.PushSelf(L);
		}
		else
		{
			lua_pushnil(L);
		}
		return 1;
	}
	static int RemoveTapNote( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int row= BeatToNoteRow(FArg(2));
		NoteData::iterator found_note= p->FindTapNote(track, row);
		if(found_note != p->GetTapsEnd(track))
		{
			p->RemoveTapNote(track, found_note);
		}
		return 0;
	}
	static int ForEachTapNote( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int start_row= LuaBeatToNoteRow(FArg(2));
		int end_row= LuaBeatToNoteRow(FArg(3));
		luaL_checktype(L, 4, LUA_TFUNCTION);
		NoteData::iterator start;
		NoteData::iterator end;
		p->GetTapNoteRange(track, start_row, end_row, start, end);
		NoteData::const_iterator track_end= p->GetTapsEnd(track);
		for(NoteData::iterator iter= start; iter != end && iter != track_end; ++iter)
		{
			lua_pushvalue(L, 4);
			iter->second.PushSelf(L);
			lua_pushnumber(L, NoteRowToBeat(iter->first));
			lua_call(L, 2, 0);
		}
		return 0;
	}
	static int ForEachTapNoteAllTracks( T* p, lua_State* L )
	{
		int start_row= LuaBeatToNoteRow(FArg(1));
		int end_row= LuaBeatToNoteRow(FArg(2));
		luaL_checktype(L, 3, LUA_TFUNCTION);
		NoteData::all_tracks_iterator iter= p->GetTapNoteRangeAllTracks(start_row, end_row, false);
		for(; !iter.IsAtEnd(); ++iter)
		{
			lua_pushvalue(L, 3);
			iter->PushSelf(L);
			lua_pushnumber(L, NoteRowToBeat(iter.Row()));
			lua_pushnumber(L, iter.Track());
			lua_call(L, 3, 0);
		}
		return 0;
	}
	static int ForEachTapNoteReverse( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int start_row= LuaBeatToNoteRow(FArg(2));
		int end_row= LuaBeatToNoteRow(FArg(3));
		luaL_checktype(L, 4, LUA_TFUNCTION);
		NoteData::reverse_iterator rstart;
		NoteData::reverse_iterator rend;
		p->GetTapNoteRangeReverse(track, start_row, end_row, rstart, rend);
		NoteData::const_reverse_iterator rtrack_end= p->GetTapsREnd(track);
		for(NoteData::reverse_iterator riter= rstart; riter != rend && riter != rtrack_end; ++riter)
		{
			lua_pushvalue(L, 4);
			riter->second.PushSelf(L);
			lua_pushnumber(L, NoteRowToBeat(riter->first));
			lua_call(L, 2, 0);
		}
		return 0;
	}
	static int ForEachTapNoteAllTracksReverse( T* p, lua_State* L )
	{
		int start_row= LuaBeatToNoteRow(FArg(1));
		int end_row= LuaBeatToNoteRow(FArg(2));
		luaL_checktype(L, 3, LUA_TFUNCTION);
		NoteData::all_tracks_reverse_iterator riter= p->GetTapNoteRangeAllTracksReverse(end_row, start_row, false);
		for(; !riter.IsAtEnd(); ++riter)
		{
			lua_pushvalue(L, 3);
			riter->PushSelf(L);
			lua_pushnumber(L, NoteRowToBeat(riter.Row()));
			lua_pushnumber(L, riter.Track());
			lua_call(L, 3, 0);
		}
		return 0;
	}
	static void push_number_if_exists(lua_State* L, bool exists, int num)
	{
		if(exists)
		{
			lua_pushnumber(L, NoteRowToBeat(num));
			return;
		}
		lua_pushnil(L);
	}
	static int GetNextTapNoteRowForTrack( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int next_row= BeatToNoteRow(FArg(2));
		bool row_exists= p->GetNextTapNoteRowForTrack(track, next_row);
		push_number_if_exists(L, row_exists, next_row);
		return 1;
	}
	static int GetNextTapNoteRowForAllTracks( T* p, lua_State* L )
	{
		int next_row= BeatToNoteRow(FArg(1));
		bool row_exists= p->GetNextTapNoteRowForAllTracks(next_row);
		push_number_if_exists(L, row_exists, next_row);
		return 1;
	}
	static int GetPrevTapNoteRowForTrack( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int next_row= LuaBeatToNoteRow(FArg(2));
		bool row_exists= p->GetPrevTapNoteRowForTrack(track, next_row);
		push_number_if_exists(L, row_exists, next_row);
		return 1;
	}
	static int GetPrevTapNoteRowForAllTracks( T* p, lua_State* L )
	{
		int next_row= LuaBeatToNoteRow(FArg(1));
		bool row_exists= p->GetPrevTapNoteRowForAllTracks(next_row);
		push_number_if_exists(L, row_exists, next_row);
		return 1;
	}
	static int MoveTapNoteTrack( T* p, lua_State* L )
	{
		p->MoveTapNoteTrack(CheckedTrackNumber(p, L, 1), CheckedTrackNumber(p, L, 2));
		return 0;
	}
	static int SetTapNote( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int row= BeatToNoteRow(FArg(2));
		TapNote tap;
		tap.ConstructFromLuaState(L, 3);
		if(tap.type == TapNoteType_Empty)
		{
			p->SetTapNote(track, row, TAP_EMPTY);
		}
		else
		{
			p->SetTapNote(track, row, tap);
		}
		return 0;
	}
	static int AddHoldNote( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int start_row= LuaBeatToNoteRow(FArg(2));
		TapNote tap;
		tap.ConstructFromLuaState(L, 3);
		int end_row= start_row + tap.iDuration;
		if(tap.type != TapNoteType_HoldHead)
		{
			luaL_error(L, "TapNote::AddHoldNote requires a TapNote of type TapNoteType_HoldHead.");
		}
		if(tap.subType >= NUM_TapNoteSubType)
		{
			luaL_error(L, "TapNote::AddHoldNote requires a TapNote with a valid TapNoteSubType set.");
		}
		p->AddHoldNote(track, start_row, end_row, tap);
		return 0;
	}
	static int ClearTrackInRange( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int start_row= LuaBeatToNoteRow(FArg(2));
		int end_row= LuaBeatToNoteRow(FArg(3));
		p->ClearRangeForTrack(start_row, end_row, track);
		return 0;
	}
	static int ClearRange( T* p, lua_State* L )
	{
		int start_row= LuaBeatToNoteRow(FArg(1));
		int end_row= LuaBeatToNoteRow(FArg(1));
		p->ClearRange(start_row, end_row);
		return 0;
	}
	static int ClearAll( T* p, lua_State* L )
	{
		p->ClearAll();
		return 0;
	}
	static int CopyRange( T* p, lua_State* L )
	{
		NoteData* src= Luna<NoteData>::check(L, 1);
		int src_begin= LuaBeatToNoteRow(FArg(2));
		int src_end= LuaBeatToNoteRow(FArg(3));
		int dst_begin= LuaBeatToNoteRow(FArg(4));
		p->CopyRange(*src, src_begin, src_end, dst_begin);
		return 0;
	}
	static int CopyAll( T* p, lua_State* L )
	{
		NoteData* src= Luna<NoteData>::check(L, 1);
		p->CopyAll(*src);
		return 0;
	}
	static int IsRowEmpty( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		lua_pushboolean(L, p->IsRowEmpty(row));
		return 1;
	}
	static int IsRangeEmpty( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int start_row= LuaBeatToNoteRow(FArg(2));
		int end_row= LuaBeatToNoteRow(FArg(3));
		lua_pushboolean(L, p->IsRangeEmpty(track, start_row, end_row));
		return 1;
	}
	static int GetNumTapsOnRow( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		lua_pushnumber(L, p->GetNumTapNonEmptyTracks(row));
		return 1;
	}
	static int GetTracksWithTapAtRow( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		set<int> track_set;
		p->GetTapNonEmptyTracks(row, track_set);
		lua_createtable(L, track_set.size(), 0);
		int tracks_pushed= 0;
		for(set<int>::iterator t= track_set.begin(); t != track_set.end(); ++t)
		{
			lua_pushnumber(L, *t);
			++tracks_pushed;
			lua_rawseti(L, -2, tracks_pushed);
		}
		return 1;
	}
	static int GetTapsAtRow( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		lua_createtable(L, 0, 0);
		int taps_returned= 0;
		for(int t= 0; t < p->GetNumTracks(); ++t)
		{
			NoteData::iterator tap_note= p->FindTapNote(t, row);
			if(tap_note != p->GetTapsEnd(t))
			{
				++taps_returned;
				lua_createtable(L, 2, 0);
				lua_pushnumber(L, t);
				lua_rawseti(L, -2, 1);
				tap_note->second.PushSelf(L);
				lua_rawseti(L, -2, 2);
				lua_rawseti(L, -2, taps_returned);
			}
		}
		return 1;
	}
	static int GetNumTracksWithTapType( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		TapNoteType type= Enum::Check<TapNoteType>(L, 2);
		bool invert= lua_toboolean(L, 3);
		lua_pushnumber(L, p->GetNumTracksWithTapType(row, type, invert));
		return 1;
	}
	static int GetNumTracksWithTapTypes( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		vector<TapNoteType> types;
		int types_index= 2;
		if(!lua_istable(L, types_index))
		{
			luaL_error(L, "GetNumTracksWithTapTypes requires table as second arg.");
		}
		size_t num_types= lua_objlen(L, types_index);
		for(size_t t= 0; t < num_types; ++t)
		{
			lua_pushnumber(L, t);
			lua_gettable(L, types_index);
			types.push_back(Enum::Check<TapNoteType>(L, -1));
			lua_pop(L, 1);
		}
		bool invert= lua_toboolean(L, 3);
		lua_pushnumber(L, p->GetNumTracksWithTapTypes(row, types, invert));
		return 1;
	}
	static int GetTracksWithTapType( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		TapNoteType type= Enum::Check<TapNoteType>(L, 2);
		bool invert= lua_toboolean(L, 3);
		vector<int> ret;
		p->GetTracksWithTapType(row, type, ret, invert);
		lua_createtable(L, ret.size(), 0);
		for(size_t t= 0; t < ret.size(); ++t)
		{
			lua_pushnumber(L, ret[t]);
			lua_rawseti(L, -2, t+1);
		}
		return 1;
	}
	static int GetTracksWithTapTypes( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		vector<TapNoteType> types;
		int types_index= 2;
		if(!lua_istable(L, types_index))
		{
			luaL_error(L, "GetNumTracksWithTapTypes requires table as second arg.");
		}
		size_t num_types= lua_objlen(L, types_index);
		for(size_t t= 0; t < num_types; ++t)
		{
			lua_pushnumber(L, t);
			lua_gettable(L, types_index);
			types.push_back(Enum::Check<TapNoteType>(L, -1));
			lua_pop(L, 1);
		}
		bool invert= lua_toboolean(L, 3);
		vector<int> ret;
		p->GetTracksWithTapTypes(row, types, ret, invert);
		for(size_t t= 0; t < ret.size(); ++t)
		{
			lua_pushnumber(L, ret[t]);
			lua_rawseti(L, -2, t+1);
		}
		return 1;
	}
	static int GetTracksHeldAtRow( T* p, lua_State* L )
	{
		int row= BeatToNoteRow(FArg(1));
		set<int> ret;
		p->GetTracksHeldAtRow(row, ret);
		int num_returned= 0;
		lua_createtable(L, ret.size(), 0);
		for(set<int>::iterator r= ret.begin(); r != ret.end(); ++r)
		{
			++num_returned;
			lua_pushnumber(L, *r);
			lua_rawseti(L, -2, num_returned);
		}
		return 1;
	}
	static int IsHoldNoteAtRow( T* p, lua_State* L )
	{
		int track= CheckedTrackNumber(p, L, 1);
		int row= BeatToNoteRow(FArg(2));
		int head_row= 0;
		bool is= p->IsHoldNoteAtRow(track, row, &head_row);
		push_number_if_exists(L, is, head_row);
		return 1;
	}
	static int IsEmpty( T* p, lua_State* L )
	{
		lua_pushboolean(L, p->IsEmpty());
		return 1;
	}
	static int IsTrackEmpty( T* p, lua_State* L )
	{
		lua_pushboolean(L, p->IsTrackEmpty(CheckedTrackNumber(p, L, 1)));
		return 1;
	}
	static int GetFirstBeat(T* p, lua_State* L)
	{
		lua_pushnumber(L, p->GetFirstBeat());
		return 1;
	}
	static int GetLastBeat(T* p, lua_State* L)
	{
		lua_pushnumber(L, p->GetLastBeat());
		return 1;
	}
#define ITERATOR_METHOD(func_name) \
	static int func_name(T* p, lua_State* L)\
	{\
		int start= BeatToNoteRow(luaL_optnumber(L, 1, 0.0));\
		int end= LuaBeatToNoteRow(luaL_optnumber(L, 2, -1.0));\
		lua_pushnumber(L, p->func_name(start, end));\
		return 1;\
	}
	ITERATOR_METHOD(GetNumTapNotes);
	ITERATOR_METHOD(GetNumMines);
	ITERATOR_METHOD(GetNumRowsWithTap);
	ITERATOR_METHOD(GetNumRowsWithTapOrHoldHead);
	ITERATOR_METHOD(GetNumHoldNotes);
	ITERATOR_METHOD(GetNumRolls);
	ITERATOR_METHOD(GetNumJumps);
	ITERATOR_METHOD(GetNumHands);
	ITERATOR_METHOD(GetNumQuads);
	ITERATOR_METHOD(GetNumLifts);
	ITERATOR_METHOD(GetNumFakes);
#undef ITERATOR_METHOD
#define ITERATOR_METHOD_WITH_MIN(func_name) \
	static int func_name(T* p, lua_State* L)\
	{\
		int start= BeatToNoteRow(luaL_optnumber(L, 1, 0.0));\
		int end= LuaBeatToNoteRow(luaL_optnumber(L, 2, -1.0));\
		int min_taps= luaL_optint(L, 3, 2);\
		lua_pushnumber(L, p->func_name(min_taps, start, end));	\
		return 1;\
	}
	// A tap is a tap, lift, or hold.
	ITERATOR_METHOD_WITH_MIN(GetNumRowsWithSimultaneousTaps);
	// A press is a tap, or hold.
	ITERATOR_METHOD_WITH_MIN(GetNumRowsWithSimultaneousPresses);
#undef ITERATOR_METHOD_WITH_MIN
#define ITERATOR_METHOD_FOR_TWO(func_name) \
	static int func_name(T* p, lua_State* L)\
	{\
		int start= BeatToNoteRow(luaL_optnumber(L, 1, 0.0));\
		int end= LuaBeatToNoteRow(luaL_optnumber(L, 2, -1.0));\
		pair<int, int> ret= p->func_name(start, end);\
		lua_createtable(L, 2, 0);\
		lua_pushnumber(L, ret.first);\
		lua_rawseti(L, -1, 1);\
		lua_pushnumber(L, ret.second);\
		lua_rawseti(L, -1, 2);\
		return 1;\
	}
	ITERATOR_METHOD_FOR_TWO(GetNumTapNotesTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumJumpsTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumHandsTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumQuadsTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumHoldNotesTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumMinesTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumRollsTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumLiftsTwoPlayer);
	ITERATOR_METHOD_FOR_TWO(GetNumFakesTwoPlayer);
#undef ITERATOR_METHOD_FOR_TWO
	static int RowNeedsAtLeastSimultaneousPresses(T* p, lua_State* L)
	{
		int row= BeatToNoteRow(FArg(1));
		int presses= IArg(2);
		lua_pushboolean(L, p->RowNeedsAtLeastSimultaneousPresses(presses, row));
		return 1;
	}
	static int RowNeedsHands(T* p, lua_State* L)
	{
		int row= BeatToNoteRow(FArg(1));
		lua_pushboolean(L, p->RowNeedsHands(row));
		return 1;
	}
	static int LoadTransformed(T* p, lua_State* L)
	{
		NoteData* src= Luna<NoteData>::check(L, 1);
		int new_num_tracks= IArg(2);
		if(new_num_tracks < 0 || new_num_tracks > MAX_NOTE_TRACKS)
		{
			luaL_error(L, "LoadTransformed: New number of tracks must be between 0 and %d.", MAX_NOTE_TRACKS);
		}
		vector<int> mapping;
		int mapping_index= 3;
		if(!lua_istable(L, mapping_index))
		{
			luaL_error(L, "LoadTransformed: Table for mapping original tracks to new tracks must be provided.");
		}
		mapping.reserve(lua_objlen(L, mapping_index));
		for(int m= 0; m < new_num_tracks; ++m)
		{
			lua_pushnumber(L, m+1);
			lua_gettable(L, mapping_index);
			int origin= IArg(-1);
			if(origin < -1 || origin >= new_num_tracks)
			{
				luaL_error(L, "LoadTransformed: Track number in mapping must be between -1 (no track) and %d (new number of tracks).", new_num_tracks);
			}
			mapping.push_back(origin);
		}
		p->LoadTransformed(*src, new_num_tracks, mapping);
		return 0;
	}

	LunaNoteData()
	{
		ADD_METHOD( GetNumTracks ); // tested
		ADD_METHOD( IsComposite );
		ADD_METHOD( GetTapNote ); // tested
		ADD_METHOD( RemoveTapNote );
		ADD_METHOD( ForEachTapNote ); // tested
		ADD_METHOD( ForEachTapNoteAllTracks ); // tested
		ADD_METHOD( ForEachTapNoteReverse );
		ADD_METHOD( ForEachTapNoteAllTracksReverse );
		ADD_METHOD( GetNextTapNoteRowForTrack ); // tested
		ADD_METHOD( GetNextTapNoteRowForAllTracks );
		ADD_METHOD( GetPrevTapNoteRowForTrack );
		ADD_METHOD( GetPrevTapNoteRowForAllTracks );
		ADD_METHOD( MoveTapNoteTrack );
		ADD_METHOD( SetTapNote ); // tested
		ADD_METHOD( AddHoldNote ); // tested
		ADD_METHOD( ClearTrackInRange );
		ADD_METHOD( ClearRange );
		ADD_METHOD( ClearAll ); // tested
		ADD_METHOD( CopyRange );
		ADD_METHOD( CopyAll );
		ADD_METHOD( IsRowEmpty );
		ADD_METHOD( IsRangeEmpty );
		ADD_METHOD( GetNumTapsOnRow );
		ADD_METHOD( GetTracksWithTapAtRow );
		ADD_METHOD( GetTapsAtRow );
		ADD_METHOD( GetNumTracksWithTapType );
		ADD_METHOD( GetNumTracksWithTapTypes );
		ADD_METHOD( GetTracksWithTapType );
		ADD_METHOD( GetTracksWithTapTypes );
		ADD_METHOD( GetTracksHeldAtRow );
		ADD_METHOD( IsHoldNoteAtRow );
		ADD_METHOD( IsEmpty );
		ADD_METHOD( IsTrackEmpty );
		ADD_METHOD( GetFirstBeat );
		ADD_METHOD( GetLastBeat );
		ADD_METHOD( GetNumTapNotes );
		ADD_METHOD( GetNumMines );
		ADD_METHOD( GetNumRowsWithTap );
		ADD_METHOD( GetNumRowsWithTapOrHoldHead );
		ADD_METHOD( GetNumHoldNotes );
		ADD_METHOD( GetNumRolls );
		ADD_METHOD( GetNumJumps );
		ADD_METHOD( GetNumHands );
		ADD_METHOD( GetNumQuads );
		ADD_METHOD( GetNumLifts );
		ADD_METHOD( GetNumFakes );
		ADD_METHOD( GetNumRowsWithSimultaneousTaps );
		ADD_METHOD( GetNumRowsWithSimultaneousPresses );
		ADD_METHOD( GetNumTapNotesTwoPlayer );
		ADD_METHOD( GetNumJumpsTwoPlayer );
		ADD_METHOD( GetNumHandsTwoPlayer );
		ADD_METHOD( GetNumQuadsTwoPlayer );
		ADD_METHOD( GetNumHoldNotesTwoPlayer );
		ADD_METHOD( GetNumMinesTwoPlayer );
		ADD_METHOD( GetNumRollsTwoPlayer );
		ADD_METHOD( GetNumLiftsTwoPlayer );
		ADD_METHOD( GetNumFakesTwoPlayer );
		ADD_METHOD( RowNeedsAtLeastSimultaneousPresses );
		ADD_METHOD( RowNeedsHands );
		ADD_METHOD( LoadTransformed );
	}
};

LUA_REGISTER_CLASS( NoteData )
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
