--[[
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Smalltalk parser/compiler library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
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
]]--

local ffi = require 'ffi'
local C = ffi.C

local module = {}
module.C = ffi.C

ffi.cdef [[
	typedef struct{ 
		int count; 
		uint8_t data[?];
	} ByteArray;

	typedef struct{ 
		int count; 
		uint16_t data[?];
	} WordArray;
	
	void St_initByteArray( ByteArray* ba, int byteLen, void* data );
        void St_initWordArray( WordArray* wa, int byteLen, void* data, int isBigEndian );
	int St_loadImage(const char* path );
]]

local ByteArray = ffi.typeof("ByteArray")
local WordArray = ffi.typeof("WordArray")
module.ByteArray = ByteArray
module.WordArray = WordArray

local arrayClasses = {} -- weak table to reference all existing ST objects
arrayClasses.__mode = "kv" 

function module.createArray( rawData, byteLen, isWord, isLittleEndian )
	local ctype
	local size
	if isWord then
		ctype = WordArray
		size = byteLen / 2
	else
		ctype = ByteArray
		size = byteLen
	end
		
	local arr
	if isWord then
		arr = ffi.new( ctype, size ) 
                ffi.C.St_initWordArray( arr, byteLen, rawData, isLittleEndian==true )
	else
		arr = ffi.new( ctype, size + 1 ) -- add one byte for the zero
		ffi.C.St_initByteArray( arr, byteLen, rawData ) 
	end
	return arr
end

module.allObjects = {} -- weak table to reference all existing ST objects
module.allObjects.__mode = "k" 

module.knownObjects = {} -- strong table to reference all ST objects with well-known OOP

function module.loadImage(path)
	return C.St_loadImage(path)
end

return module
