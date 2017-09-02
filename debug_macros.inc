	; ------------------------------------------------------------------------
	; HeavyThing x86_64 assembly language library and showcase programs
	; Copyright © 2015, 2016 2 Ton Digital 
	; Homepage: https://2ton.com.au/
	; Author: Jeff Marrison <jeff@2ton.com.au>
	;       
	; This file is part of the HeavyThing library.
	;       
	; HeavyThing is free software: you can redistribute it and/or modify
	; it under the terms of the GNU General Public License, or
	; (at your option) any later version.
	;       
	; HeavyThing is distributed in the hope that it will be useful, 
	; but WITHOUT ANY WARRANTY; without even the implied warranty of
	; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
	; GNU General Public License for more details.
	;       
	; You should have received a copy of the GNU General Public License along
	; with the HeavyThing library. If not, see <http://www.gnu.org/licenses/>.
	; ------------------------------------------------------------------------
	; debug_macros.inc: useful macro(s) for debugging that I otherwise copy
	; around.

macro register_debug preface*, reg* {
	local ..continue, ..string
	push	rax rcx rdx rdi rsi r8 r9 r10 r11
	sub	rsp, 8
	mov	rdi, reg
	mov	esi, 16
	call	string$from_unsigned
	mov	[rsp], rax
	mov	rdi, ..string
	call	string$to_stdout
	mov	rdi, [rsp]
	call	string$to_stdoutln
	mov	rdi, [rsp]
	call	heap$free
	add	rsp, 8
	pop	r11 r10 r9 r8 rsi rdi rdx rcx rax
	jmp	..continue
cleartext ..string, preface
calign
..continue:
}
