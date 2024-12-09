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
	
	int PAL3_init(uint8_t* b, int len, int w, int h);
	int PAL3_deinit();
	int PAL3_processEvents(int sleep);
	int PAL3_nextEvent();
	int PAL3_getTime();
	int PAL3_setCursorBitmap(uint8_t* b, int w, int h);
	void PAL3_setCursorPos(int x, int y);
	void PAL3_updateArea(int x,int y,int w,int h,int cx,int cy,int cw,int ch);
	
	typedef struct Bitmap {
		uint16_t pixWidth, pixHeight;
		uint16_t wordLen;
		uint8_t* buf;
	}Bitmap;
	
	typedef struct Context
	{
		const Bitmap* sourceBits; // 0, may be null
		Bitmap* destBits; // 1
		const Bitmap* halftoneBits; // 2, may be null
		int16_t combinationRule; // 3
		int16_t destX, clipX, clipWidth, sourceX, width; // 4, 5, 6, 7, 8
		int16_t destY, clipY, clipHeight, sourceY, height; // 9, 10, 11, 12, 13
	} Context;
	
	void BitBlt_copyBits(Context* c);
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
	return C.PAL3_processEvents(0)
end

function Display_nextEvent()
	return C.PAL3_nextEvent()
end

function Display_close()
	C.PAL3_deinit()
end

function Display_updateArea(x,y,w,h,cx,cy,cw,ch)
	C.PAL3_updateArea(x,y,w,h,cx,cy,cw,ch)
end

local function toBitmap(bm)
	if bm == nil then return nil end
	local res = ffi.new( ffi.typeof("Bitmap") )
	res.pixWidth = bm[0]
	res.pixHeight = bm[1]
	res.wordLen = ffi.sizeof(bm[2]) / 2
	res.buf = bm[2]
	return res
end

function Display_copyBits(ctx)
	local c = ffi.new( ffi.typeof("Context") )
	
	c.sourceBits = toBitmap(ctx[0])
	c.destBits = toBitmap(ctx[1])
	c.halftoneBits = toBitmap(ctx[2])
	
	c.combinationRule = ctx[3]
	c.destX = ctx[4]
	c.clipX = ctx[5]
	c.clipWidth = ctx[6]
	c.sourceX = ctx[7]
	c.width = ctx[8]
	c.destY = ctx[9]
	c.clipY = ctx[10]
	c.clipHeight = ctx[11]
	c.sourceY = ctx[12]
	c.height = ctx[13]
	C.BitBlt_copyBits(c)
end
