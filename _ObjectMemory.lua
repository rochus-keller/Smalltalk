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

local ffi = require 'ffi'
local C = ffi.C

ffi.cdef[[
	typedef union Convert_float_int {
		uint32_t i;
		float f;
	} Convert_float_int;
]]

local helper = ffi.new( ffi.typeof("Convert_float_int") )

function ObjectMemory_realToInt(x)
	helper.f = x
	return helper.i
end

function ObjectMemory_intToReal(x)
	helper.i = x
	return helper.f
end
