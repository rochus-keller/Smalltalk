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
	
	int PAL3_init(uint8_t* b, int len, int w, int h);
	int PAL3_deinit();
	int PAL3_processEvents(int sleep);
	int PAL3_nextEvent();
	int PAL3_getTime();
	int PAL3_setCursorBitmap(uint8_t* b, int w, int h);
	void PAL3_setCursorPos(int x, int y);
	int PAL3_eventPending();
	void PAL3_updateArea(int x,int y,int w,int h,int cx,int cy,int cw,int ch);
]]

local bitmap

function Display_getTicks()
	return C.PAL3_getTime() * 1000
end

-- Bitmap = record pixWidth(0), pixHeight(1): integer; buffer(2): array of byte end
function Display_setScreenBitmap(bm)
	bitmap = bm
	C.PAL3_init(bm[2], ffi.sizeof(bm[2]), bm[0], bm[1])
end

function Display_setCursorBitmap(bm)
	C.PAL3_setCursorBitmap(bm[2], bm[0], bm[1])
end

function Display_setCursorPos(x, y)
	C.PAL3_setCursorPos(x,y)
end

function Display_getScreenBitmap()
	return bitmap
end

function Display_processEvents()
	return C.PAL3_processEvents(0) ~= 0
end

function Display_nextEvent()
	return C.PAL3_nextEvent()
end

function Display_close()
	C.PAL3_deinit()
end

function Display_eventPending()
	return C.PAL3_eventPending() > 0
end

function Display_updateArea(x,y,w,h,cx,cy,cw,ch)
	C.PAL3_updateArea(x,y,w,h,cx,cy,cw,ch)
end
