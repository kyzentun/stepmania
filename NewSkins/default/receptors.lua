-- A layer file must return a function that takes a button list.  When the
-- noteskin is loaded, this function will be called with the list of buttons.
-- The function must return a table containing an actor for each column.
-- The notefield will set the x value of each column after loading.
-- These are full actors and their update functions will be called every
-- frame, so they do not have the limitations that notes have.
local red= {1, 0, 0, 1}
local white= {1, 1, 1, 1}
return function(button_list)
	local ret= {}
	local rots= {Left= 90, Down= 0, Up= 180, Right= 270}
	for i, button in ipairs(button_list) do
		ret[i]= Def.Sprite{
			Texture= "receptor.png", InitCommand= function(self)
				self:rotationz(rots[button] or 0):effectclock("beat")
			end,
			-- The Pressed command is the way to respond to the column being
			-- pressed.  The param table only has one element: "on".  If that
			-- element is true, the column is now pressed.
			PressedCommand= function(self, param)
				if param.on then
					self:zoom(.75)
				else
					self:zoom(1)
				end
			end,
			-- The Upcoming command happens every frame, to update the distance in
			-- beats to the nearest note in the column.  If the notefield does not
			-- detect a note within its draw distance, it sends a distance of 1000.
			-- The draw distance of the notefield varies with modifiers such as
			-- speed and boomerang, and might be controlled by lua.
			-- The SM 5.0.x notefield ShowNoteUpcoming and HideNoteUpcoming
			-- commands with no distance information.
			-- Because this command is executed while drawing the notes layer,
			-- layers that are beneath the notes will have the distance from the
			-- previous frame, while layers above the notes will have the distance
			-- for the current frame.
			UpcomingCommand= function(self, param)
				if param.distance < 4 then
					self:diffuse(lerp_color(param.distance / 4, white, red))
				else
					self:diffuse(white)
				end
			end
		}
	end
	return ret
end
