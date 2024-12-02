--[[*
* Copyright 2024 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Luon Smalltalk-80 VM.
*
* The following is the license that applies to this copy of the
* application. For a license to use the application under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
*]]--


local ffi = require("ffi")
local C = ffi.C

ffi.cdef[[
	typedef uint8_t CharArray[?];
	
	int PAL2_init(uint8_t* b, int l, int w, int h);
	int PAL2_deinit();
	int PAL2_processEvents(int sleep);
	int PAL2_nextEvent();
	int PAL2_getTime();
	int PAL2_setCursorBitmap(uint8_t* b, int l, int w, int h);
	void PAL2_setCursorPos(int x, int y);
]]

local bitmap

function Display_getTicks()
	return C.PAL2_getTime() * 1000
end

-- Bitmap = record pixWidth, pixHeight, pixLineWidth, wordLen: integer; buffer: array of byte end
function Display_setScreenBitmap(bm)
	bitmap = bm
	C.PAL2_init(bm[4], bm[2] * 2, bm[0], bm[1])
end

function Display_setCursorBitmap(bm)
	C.PAL2_setCursorBitmap(bm[4], bm[2] * 2, bm[0], bm[1])
end

function Display_setCursorPos(x, y)
	C.PAL2_setCursorPos(x,y)
end

function Display_getScreenBitmap()
	return bitmap
end

function Display_processEvents()
	C.PAL2_processEvents(20)
end

function Display_nextEvent()
	return C.PAL2_nextEvent()
end
