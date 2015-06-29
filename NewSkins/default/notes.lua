-- See explanation where tap_state_map is generated.  This function will
-- probably be moved to some scripts file specific to noteskins so it won't
-- have to be in every noteskin.
local function generate_state_map_from_quantums(parts_per_beat, quantums)
	-- Use the last entry as a default to allow a shorter list.  This means a
	-- noteskin that only goes down to 64ths can pass a parts_per_beat of 48
	-- and leave off the entries for 96ths and 192nds.
	local default_quantum= quantums[#quantums].states
	local state_map= {}
	for part= 0, parts_per_beat - 1 do
		for quant_index, quant in ipairs(quantums) do
			-- spacing is how many parts there are between occurances of the
			-- current quantization.
			local spacing= parts_per_beat / quant.per_beat
			-- If spacing does not come up as an integer, then the modulus won't be
			-- zero, which means the quantization is finer than the parts per beat
			-- and should be ignored.  This allows having a table of quantums with
			-- extra entries, and controlling the fineness by adjusting parts per
			-- beat.
			if part % spacing == 0 then
				state_map[part+1]= quant.states
				break
			end
		end
		if not state_map[part+1] then
			state_map[part+1]= default_quantum
		end
	end
	return state_map
end

-- The noteskin file returns a function that will be called to load the
-- actors for the noteskin.  The function will be passed a list of buttons.
-- Each element in button_list is the name of the button that will be used
-- for that column.
-- A button name may be repeated, for instance when the notefield is for
-- doubles, which has Left, Down, Up, Right, Left, Down, Up, Right.
-- The function must return a table containing all the information and actors
-- needed for the columns given.  Missing information or actors will be
-- filled with zeros or blank actors.
return function(button_list)
	-- rots is a convenience conversion table to easily take care of buttons
	-- that should be rotated.
	local rots= {Left= 90, Down= 0, Up= 180, Right= 270}
	-- A state_map tells Stepmania what frames to use for the quantization of
	-- a note and the current beat.
	-- Two variables are used to find the frame to use in the state_map:
	-- 1. The quantization.  2. The current beat of the music.
	-- The quantization of a note is how far it is from being directly on the
	-- beat.  A note on beat 5.0 has a quantization of 0.  A note on beat
	-- 5.75 has a quantization of 0.75.
	-- The beat is the current beat of the music, shifted to be between 0 and
	-- 1.  So if the current beat is 2.33, 0.33 is used to find the frame to
	-- use.
	-- First, Stepmania finds the entry for the note's quantization.
	-- In code, it uses this formula:
	--   entry_index= floor(quantization * #state_map)
	-- Long explanation:
	--   There is one list of frames to use for each quantization the noteskin
	--   supports.
	--   A flat noteskin would have only one entry.
	--   A noteskin that only supports 4ths (0 quantization) and 8ths
	--   (0.5 quantization) would have two entries.  Everything greater than
	--   or equal to 0 and less than 0.5 quantization would use the first
	--   entry, and everything greater than or equal 0.5 and less than 1
	--   quantization would use the second entry.
	--   This means that the noteskin system does not impose a quantization
	--   limit, though other parts of Stepmania do.  It also means that the
	--   16th after a beat (quantization 0.25) and the 16th before a beat
	--   (quantization 0.75) can look different in non-vivid noteskins.
	-- Secondly, Stepmania finds the state in the entry for the current beat.
	-- The vivid flag is used here to allow vivid noteskins to make different
	-- quantizations start at different points in the animation.
	-- In code, it uses this formula:
	--   if vivid then
	--     state_index= floor((quantization + beat) * #entry)
	--   else
	--     state_index= floor(beat * #entry)
	--   end
	-- The index is then wrapped so that quantization 0.75 + beat 0.75
	-- doesn't go off the end of the entry.
	-- Long explanation:
	--   Each quantization has a set of frames that it cycles through.  The
	--   current beat is used to pick the frame.  For vivid noteskins, the
	--   quantization is added to the beat to pick the frame.  If
	--   quantization+beat is greater than 1, it is shifted to put it between
	--   0 and 1.
	--   The frames are spaced equally through the beat.
	--   If there are two frames, the first frame is used while the beat is
	--   greater than or equal to 0 and less than 0.5.  The second frame is
	--   used from 0.5 to 1.
	-- This example has 4 quantizations.
	-- common_state_map is for convenience making the noteskin.  Each
	-- NewSkinPart in each column is allowed to have its own state map, to allow
	-- them to animate differently, but that is usually not desired.
	-- To make the state maps slightly more readable, this example first sets
	-- some variables for the common quantizations.  Then those are used to set
	-- the entries in the state map.

	-- This generates a state map suitable for dividing a beat into 240 parts.
	local parts_per_beat= 48
	-- Factors of 240: 2, 2, 2, 2, 3, 5
	-- Each quantization occurs a given number of times per beat.
	-- They must be arranged in ascending order of times per beat for the
	-- correct quantization to be assigned.
	local tap_quantums= {
		{per_beat= 1, states= {1, 2}}, -- 4th
		{per_beat= 2, states= {3, 4}}, -- 8th
		{per_beat= 3, states= {5, 6}}, -- 12th
		{per_beat= 4, states= {7, 8}}, -- 16th
		{per_beat= 6, states= {9, 10}}, -- 24th
		{per_beat= 8, states= {11, 12}}, -- 32nd
		{per_beat= 12, states= {13, 14}}, -- 48th
		{per_beat= 16, states= {15, 16}}, -- 64th
	}
	-- generate_state_map_from_quantums is a convenience function for using a
	-- list of quantizations to generate a state map for the note.  Fill
	-- tap_quantums with the quantizations you want to support, then call
	-- generate_state_map_from_quantums to create the state map.
	-- If a quantum is not a factor of parts_per_beat, it will not be used.
	-- The last entry in tap_quantums will be used for parts that don't fit a
	-- known quantization.
	local tap_state_map= generate_state_map_from_quantums(parts_per_beat, tap_quantums)
	-- Mines only have a single frame in the graphics.
	local mine_state_map= {{1}}
	-- Holds have active and inactive states, so they need a different state
	-- map.
	local hold_quantums= {
		{per_beat= 1, states= {1, 2}}, -- 4th
		{per_beat= 2, states= {5, 6}}, -- 8th
		{per_beat= 3, states= {9, 10}}, -- 12th
		{per_beat= 4, states= {13, 14}}, -- 16th
		{per_beat= 6, states= {17, 18}}, -- 24th
		{per_beat= 8, states= {21, 22}}, -- 32nd
		{per_beat= 12, states= {25, 26}}, -- 48th
		{per_beat= 16, states= {29, 30}}, -- 64th
	}
	local active_state_map= generate_state_map_from_quantums(parts_per_beat, hold_quantums)
	local inactive_state_map= {}
	-- To make creating the inactive state map easier, the inactive states are
	-- assumed to be the two states after the active states.
	for ssi= 1, #active_state_map do
		local act_entry= active_state_map[ssi]
		local entry= {}
		for di= 1, #act_entry do
			entry[di]= act_entry[di] + 2
		end
		inactive_state_map[ssi]= entry
	end
	-- Taps are handled by a quantized_tap structure.  A quantized_tap contains
	-- an actor for the tap, a state map to use to quantize it, and a vivid
	-- memory of its world that was destroyed.
	-- If the vivid flag is not set, it will be treated as false.
	-- If you are making a vivid noteskin, you probably want to use the global
	-- vivid_operation flag that is explained later.
	-- Each element in taps is the set of quantized_taps to use for that column.
	-- There is one quantized_tap for each noteskin part.

	-- Holds are not actors at all.  Instead, they are handled through a series
	-- of tubes, like a big truck.
	-- A quantized_hold has a state map (same as a tap's state map), a set of
	-- textures for its layers, and its own vivid love for the world it
	-- protects.
	-- A hold texture must have the top cap, body, and bottom cap in each
	-- frame.  The top cap must be 1/6 of the frame height, the body must be
	-- 2/3 of the frame height, and the bottom cap must be the remaining 1/6 of
	-- the frame.  The second half of the body must be identical to the first.
	-- This is so that during rendering, the first half of the body section can
	-- be repeated to cover the length of the hold.  Putting the top cap and
	-- bottom cap in the same frame as the body avoids gap and seam problems.

	-- Each column has a set of quantized_taps for each noteskin part, a
	-- quantized_hold for the inactive and inactive states of each hold subtype
	-- (holds and rolls), and a rotations table that controls how the parts are
	-- rotated.
	local columns= {}
	for i, button in ipairs(button_list) do
		columns[i]= {
			taps= {
				NewSkinTapPart_Tap= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= "tap_note 2x8.png"}},
				NewSkinTapPart_Mine= {
					state_map= mine_state_map,
					actor= Def.Sprite{Texture= "mine.png"}},
				NewSkinTapPart_Lift= { -- fuck lifts
					state_map= mine_state_map,
					actor= Def.Sprite{Texture= "mine.png"}},
			},
			-- Not used by this noteskin:  optional_taps.
			-- The optional_taps table is here in a comment as an example.  Since
			-- this noteskin does not use heads or tails, the notefield draws taps
			-- instead of heads and nothing for tails.
			-- optional_taps is the field used for heads and tails.  The elements
			-- in it are indexed by hold subtype and split into head and tail.
			-- When the notefield needs a head, it first looks for a head with the
			-- given subtype.  If that doesn't exist, it looks for the hold head.
			-- If that doesn't exist, it falls back on a normal tap.
			-- Tails fall back in a similar way, except they come up blank if the
			-- hold tail piece doesn't exist.
			--[[
			optional_taps= {
				-- This will be used if NewSkinTapOptionalPart_RollHead or
				-- NewSkinTapOptionalPart_CheckpointHead does not exist.
				NewSkinTapOptionalPart_HoldHead= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= "hold_head 2x8.png"}},
				-- This will be used if NewSkinTapOptionalPart_RollTail or
				-- NewSkinTapOptionalPart_CheckpointTail does not exist.
				NewSkinTapOptionalPart_HoldTail= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= "hold_tail 2x8.png"}},
				NewSkinTapOptionalPart_RollHead= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= "roll_head 2x8.png"}},
				NewSkinTapOptionalPart_RollTail= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= "roll_tail 2x8.png"}},
				NewSkinTapOptionalPart_CheckpointHead= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= "checkpoint_head 2x8.png"}},
				NewSkinTapOptionalPart_CheckpointTail= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= "checkpoint_tail 2x8.png"}},
			},
			]]
			holds= {
				-- The inactive states use the inactive_state_map while the active
				-- states use the active_state_map.  The state maps use different
				-- frames, so the active and inactive states look different while
				-- using the same texture.
				TapNoteSubType_Hold= {
					-- This is the quantized_hold for the inactive state of holds.
					{
						state_map= inactive_state_map,
						-- textures is a table so that the hold can be rendered as
						-- multiple layers.  The textures must all be the same size and
						-- frame dimensions.  The same frame from each will be rendered,
						-- in order.  This replaces the HoldActiveIsAddLayer flag that
						-- was in the old noteskin format.
						textures= {button:lower() .. "_hold 8x4.png"},
					},
					-- This is the quantized_hold for the active state of holds.
					{
						state_map= active_state_map,
						textures= {button:lower() .. "_hold 8x4.png"},
					},
				},
				TapNoteSubType_Roll= {
					{
						state_map= inactive_state_map,
						textures= {button:lower() .. "_hold 8x4.png"},
					},
					{
						state_map= active_state_map,
						textures= {button:lower() .. "_hold 8x4.png"},
					},
				},
			},
			-- rotations controls what parts are rotated and how far.  This is used
			-- to make taps and lifts rotate and mines not rotate at all.
			rotations= {
				NewSkinTapPart_Tap= rots[button],
				NewSkinTapPart_Mine= 0,
				NewSkinTapPart_Lift= 0,
			},
		}
	end
	return {
		columns= columns,
		-- In addition to each part having its own vivid flag, there is this
		-- global flag.  If this global flag is true or false, the flags for all
		-- the parts will be set to the same.  If you have different parts with
		-- different vivid flags, do not set vivid_operation.
		-- This noteskin isn't vivid in the traditional sense, but setting the
		-- vivid flag makes the notes at a different part of the beat start at a
		-- different part of the animation.  So the first 16th after the beat has
		-- the white outline, and the one before the next beat has the black
		-- outline.
		vivid_operation= true, -- output 200%
	}
end
