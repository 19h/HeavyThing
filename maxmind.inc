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
	; maxmind.inc: http://dev.maxmind.com/geoip/geoip2/geolite2/ support
	;
	; The routines herein expect CSV formatted GeoLite2 for its data, and this
	; only parses for a City, Country IPv4 lookup (we don't bother with the
	; rest of the GeoLite2 data).
	; 

if used maxmind$new | defined include_everything

maxmind_blockmap_ofs = 0		; unsignedmap keyed by IPv4, value is endIP and locID
maxmind_locmap_ofs = 8			; unsignedmap keyed by locID, value is string City, Country
maxmind_cities_ofs = 16			; stringmap, keyed by City, Country ... no value
maxmind_size = 24



	; two arguments: rdi == string location of GeoLite2-City-Blocks-IPv4.csv filename,
	;    rsi == string location of the GeoLite2-City-Locations csv filename
	; returns a new maxmind object, or null if files invalid/not found
	; (NOTE: sane maxmind files are required, no bounds checking is performed)
falign
maxmind$new:
	prolog	maxmind$new
	push	rbx r12 r13 r14 r15
	mov	r12, rdi
	mov	r13, rsi
	xor	esi, esi
	call	privmapped$new
	test	rax, rax
	jz	.failexit
	mov	r12, rax
	mov	r14, [rax+privmapped_base_ofs]
	mov	r15, [rax+privmapped_size_ofs]
	mov	rdi, r13
	xor	esi, esi
	test	r15, r15
	jz	.cityfail
	call	privmapped$new
	test	rax, rax
	jz	.cityfail
	mov	r13, rax
	cmp	qword [rax+privmapped_size_ofs], 0
	je	.bothfail
	; both privmapped objects are valid
	mov	edi, maxmind_size
	call	heap$alloc
	mov	rbx, rax
	xor	edi, edi
	call	unsignedmap$new
	mov	[rbx+maxmind_blockmap_ofs], rax
	xor	edi, edi
	call	unsignedmap$new
	mov	[rbx+maxmind_locmap_ofs], rax
	xor	edi, edi
	call	stringmap$new
	mov	[rbx+maxmind_cities_ofs], rax

	push	rbp r12
	sub	rsp, 8
.blocksouter_reset:
	xor	ebp, ebp
	mov	r12, rsp
calign
.blocksouter:
	movzx	eax, byte [r14]
	cmp	eax, '.'
	je	.blocksouter_separator
	cmp	eax, '/'
	je	.blocksouter_addrdone
	cmp	eax, '0'
	jb	.skipblockline
	cmp	eax, '9'
	ja	.skipblockline
	; otherwise, a numeric digit
	sub	eax, '0'
	; mul whatever is in ebp by 10 then add this digit
	imul	ebp, ebp, 10
	add	ebp, eax
	add	r14, 1
	sub	r15, 1
	jnz	.blocksouter
.blocksdone:
	; locations next up
	mov	r14, [r13+privmapped_base_ofs]
	mov	r15, [r13+privmapped_size_ofs]
.cityouter_reset:
	xor	ebp, ebp
	mov	r12d, 5
calign
.cityouter:
	movzx	eax, byte [r14]
	cmp	eax, ','
	je	.cityouter_locIDdone
	cmp	eax, '0'
	jb	.skipcityline
	cmp	eax, '9'
	ja	.skipcityline
	; otherwise, a numeric digit
	sub	eax, '0'
	imul	ebp, ebp, 10
	add	ebp, eax
	add	r14, 1
	sub	r15, 1
	jnz	.cityouter
.citydone:
	; cleanup our stack
	add	rsp, 8
	pop	r12 rbp
	; free our privmapped goods
	mov	rdi, r13
	call	privmapped$destroy
	mov	rdi, r12
	call	privmapped$destroy
	; return is rbx
	mov	rax, rbx
.failexit:
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.cityouter_locIDdone:
	movzx	eax, byte [r14]
	cmp	eax, ','
	je	.cityouter_countryscan
	add	r14, 1
	sub	r15, 1
	jnz	.cityouter_locIDdone
	jz	.citydone
calign
.cityouter_countryscan:
	add	r14, 1
	sub	r15, 1
	jz	.citydone
	sub	r12d, 1
	jnz	.cityouter_locIDdone
	; r14 now sitting on the country_name
	mov	[rsp], r14
	; walk forward again til next comma
calign
.cityouter_countryendscan:
	movzx	eax, byte [r14]
	cmp	eax, ','
	je	.cityouter_countryend
	add	r14, 1
	sub	r15, 1
	jnz	.cityouter_countryendscan
	jz	.citydone
calign
.cityouter_countryend:
	; r14 now sitting on the comma after the country
	mov	rdi, [rsp]
	mov	rsi, r14
	sub	rsi, rdi
	call	string$from_utf8
	; stick that somewhere temporarily
	mov	[r13+privmapped_user_ofs], rax
	; so the user var of our privmapped object contains the country name
	; we are still sitting on the comma after the country... so we
	; need to skip 5 more commas
	mov	r12d, 5
calign
.cityouter_citysearch:
	movzx	eax, byte [r14]
	cmp	eax, ','
	je	.cityouter_cityscan
	add	r14, 1
	sub	r15, 1
	jnz	.cityouter_citysearch
	jz	.citydone
calign
.cityouter_cityscan:
	add	r14, 1
	sub	r15, 1
	jz	.citydone
	sub	r12d, 1
	jnz	.cityouter_citysearch
	; r14 now sitting on the city_name
	mov	[rsp], r14
	; walk forward again til next comma
calign
.cityouter_cityendscan:
	movzx	eax, byte [r14]
	cmp	eax, ','
	je	.cityouter_cityend
	add	r14, 1
	sub	r15, 1
	jnz	.cityouter_cityendscan
	jz	.citydone
calign
.cityouter_cityend:
	; r14 now sitting on the comma after the city
	mov	rdi, [rsp]
	mov	rsi, r14
	sub	rsi, rdi
	call	string$from_utf8
	mov	r12, rax		; save that
	cmp	qword [r12], 0
	je	.cityouter_countryonly
	mov	rdi, rax
	mov	rsi, .commaspace
	call	string$concat
	mov	rdi, r12
	mov	r12, rax
	call	heap$free
.cityouter_countryonly:
	mov	rdi, r12
	mov	rsi, [r13+privmapped_user_ofs]
	call	string$concat
	mov	rdi, r12
	mov	r12, rax
	call	heap$free
	mov	rdi, [r13+privmapped_user_ofs]
	call	heap$free
	; so now, r12 contains City, Country
	; and ebp still contains our locID key
	; get rid of double quotes if we have any
	mov	rdi, r12
	mov	esi, '"'
	call	string$indexof_charcode
	cmp	rax, 0
	jl	.noquotes
	mov	rdi, r12
	mov	rsi, .doublequote
	mov	rdx, .emptystring
	call	string$replace
	mov	rdi, r12
	mov	r12, rax
	call	heap$free
.noquotes:
	; first up, see if we already know about this city, country in our cities map
	mov	rdi, [rbx+maxmind_cities_ofs]
	mov	rsi, r12
	call	stringmap$find
	test	rax, rax
	jz	.cityouter_newcity
	; otherwise, we can reuse the _avlofs_key from this node
	mov	rdi, r12
	mov	r12, [rax+_avlofs_key]
	call	heap$free
.cityouter_insert:
	; insert into our locmap
	mov	rdi, [rbx+maxmind_locmap_ofs]
	mov	esi, ebp	; locID
	mov	rdx, r12	; City, Country string
	call	unsignedmap$insert_unique
	; fallthrough to skipcityline
calign
.skipcityline:
	movzx	eax, byte [r14]
	add	r14, 1
	sub	r15, 1
	jz	.citydone
	cmp	eax, 10
	jne	.skipcityline
	jmp	.cityouter_reset
cleartext .commaspace, ', '
cleartext .doublequote, '"'
cleartext .emptystring, ''
calign
.cityouter_newcity:
	; insert our r12 string as the key into our cities map
	mov	rdi, [rbx+maxmind_cities_ofs]
	mov	rsi, r12
	xor	edx, edx	; we don't care about the value (it is a set)
	call	stringmap$insert_unique
	; go ahead and insert
	jmp	.cityouter_insert
calign
.skipblockline:
	movzx	eax, byte [r14]
	add	r14, 1
	sub	r15, 1
	jz	.blocksdone
	cmp	eax, 10
	jne	.skipblockline
	jmp	.blocksouter_reset
calign
.blocksouter_addrdone:
	; put the final byte into r12
	mov	eax, ebp
	mov	byte [r12], al
	sub	r12, 3
	mov	r12d, [r12]
	xor	ebp, ebp
	; r12d now contains our IPv4 key, netblock size is next (we are sitting on the /)
	add	r14, 1
	sub	r15, 1
	jz	.blocksdone
calign
.blocksouter_netblock:
	movzx	eax, byte [r14]
	cmp	eax, ','
	je	.blocksouter_netblockdone
	cmp	eax, '0'
	jb	.skipblockline
	cmp	eax, '9'
	ja	.skipblockline
	sub	eax, '0'
	imul	ebp, ebp, 10
	add	ebp, eax
	add	r14, 1
	sub	r15, 1
	jnz	.blocksouter_netblock
	jz	.blocksdone
calign
.blocksouter_netblockdone:
	; we are sitting on a , which means ebp contains our netblock size
	mov	ecx, 32
	mov	eax, 1
	sub	ecx, ebp
	shl	rax, cl
	sub	rax, 1
	; bswap the network address so we can numerically add this amount to it
	mov	edx, r12d
	bswap	edx
	; eax now contains our block size that we'll also keep track of
	lea	ebp, [edx+eax]		; end address
	; we still need to parse out the locID, so we can save ebp into our stack
	mov	[rsp], rbp		; [rsp] == end address
	xor	ebp, ebp
	add	r14, 1
	sub	r15, 1
	jz	.blocksdone
calign
.blocksouter_locID:
	movzx	eax, byte [r14]
	cmp	eax, ','
	je	.blocksouter_locIDdone
	cmp	eax, '0'
	jb	.skipblockline
	cmp	eax, '9'
	ja	.skipblockline
	sub	eax, '0'
	imul	ebp, ebp, 10
	add	ebp, eax
	add	r14, 1
	sub	r15, 1
	jnz	.blocksouter_locID
	jz	.blocksdone
calign
.blocksouter_locIDdone:
	; we are sitting on a , which means ebp contains our locID
	bswap	r12d
	shl	rbp, 32			; upper 32 bits == locID
	or	rbp, [rsp]		; lower 32 bits == end address
	mov	rdi, [rbx+maxmind_blockmap_ofs]
	mov	esi, r12d
	mov	rdx, rbp
	call	unsignedmap$insert_unique
	; we aren't interested in the remainder of the line:
	jmp	.skipblockline
calign
.blocksouter_separator:
	mov	eax, ebp
	xor	ebp, ebp
	mov	byte [r12], al
	add	r12, 1
	add	r14, 1
	sub	r15, 1
	jz	.blocksdone
	jmp	.blocksouter
.bothfail:
	mov	rdi, r13
	call	privmapped$destroy
.cityfail:
	mov	rdi, r12
	call	privmapped$destroy
	xor	eax, eax
	pop	r15 r14 r13 r12 rbx
	epilog

end if



if used maxmind$destroy | defined include_everything
	; single argument in rdi: maxmind object to destroy
falign
maxmind$destroy:
	prolog	maxmind$destroy
	push	rbx
	mov	rbx, rdi
	mov	rdi, [rdi+maxmind_cities_ofs]
	mov	rsi, heap$free
	call	stringmap$clear
	mov	rdi, [rbx+maxmind_cities_ofs]
	call	heap$free
	mov	rdi, [rbx+maxmind_locmap_ofs]
	xor	esi, esi
	call	unsignedmap$clear
	mov	rdi, [rbx+maxmind_locmap_ofs]
	call	heap$free
	mov	rdi, [rbx+maxmind_blockmap_ofs]
	xor	esi, esi
	call	unsignedmap$clear
	mov	rdi, [rbx+maxmind_blockmap_ofs]
	call	heap$free
	mov	rdi, rbx
	call	heap$free
	pop	rbx
	epilog

end if

if used maxmind$citycountry_ipv4 | defined include_everything
	; two arguments: rdi == maxmind object, esi == numeric ipv4 address
	; returns a string of City, Country ... or Country by itself if no City, or empty string if not found
falign
maxmind$citycountry_ipv4:
	prolog	maxmind$citycountry_ipv4
	bswap	esi				; first octet of IPv4 == highest
	push	rbx r12
	mov	rbx, rdi
	mov	r12d, esi
	; first up: lowerbound returns us the first >= our address:
	mov	rdi, [rdi+maxmind_blockmap_ofs]
	call	unsignedmap$lowerbound
	test	rax, rax
	jz	.retempty
	; if the node's key > our address, go back one block
	cmp	r12d, [rax+_avlofs_key]
	jb	.backone			; if our address is < this node, go back one
.nodeokay:
	; see if our address is inside the lower 32 bits of the blockmap
	cmp	r12d, [rax+_avlofs_value]	; lower 32 bits == end address
	ja	.retempty
	; otherwise, upper 32 bits is our locID:
	mov	rdi, [rbx+maxmind_locmap_ofs]
	mov	esi, [rax+_avlofs_value+4]	; upper 32 bits == locID
	call	unsignedmap$find_value
	test	eax, eax
	jz	.retempty
	mov	rax, rdx
	pop	r12 rbx
	epilog
.backone:
	mov	rax, [rax+_avlofs_prev]
	test	rax, rax
	jnz	.nodeokay
.retempty:
	mov	rax, .emptystring
	pop	r12 rbx
	epilog
cleartext .emptystring, ''

end if

if used maxmind$citycountry | defined include_everything
	; two arguments: rdi == maxmind object, rsi == string ipv4 address
	; returns a string of City, Country ... or Country by itself if no City, or empty string if not found
falign
maxmind$citycountry:
	prolog	maxmind$citycountry
	mov	rdx, [rsi]
	test	rdx, rdx
	jz	.retempty
	add	rsi, 8
	xor	ecx, ecx
calign
.dig1:
if string_bits = 32
	mov	eax, [rsi]
	add	rsi, 4
else
	movzx	eax, word [rsi]
	add	rsi, 2
end if
	cmp	eax, '.'
	je	.dig1done
	cmp	eax, '0'
	jb	.retempty
	cmp	eax, '9'
	ja	.retempty
	sub	eax, '0'
	imul	ecx, ecx, 10
	add	ecx, eax
	sub	edx, 1
	jnz	.dig1
.retempty:
	mov	rax, .emptystring
	epilog
cleartext .emptystring, ''
calign
.dig1done:
	sub	edx, 1
	jz	.retempty
	; first octet is in ecx
	mov	r8d, ecx
	xor	ecx, ecx
calign
.dig2:
if string_bits = 32
	mov	eax, [rsi]
	add	rsi, 4
else
	movzx	eax, word [rsi]
	add	rsi, 2
end if
	cmp	eax, '.'
	je	.dig2done
	cmp	eax, '0'
	jb	.retempty
	cmp	eax, '9'
	ja	.retempty
	sub	eax, '0'
	imul	ecx, ecx, 10
	add	ecx, eax
	sub	edx, 1
	jnz	.dig2
	jmp	.retempty
calign
.dig2done:
	sub	edx, 1
	jz	.retempty
	; second octet is in ecx
	shl	ecx, 8
	or	r8d, ecx
	xor	ecx, ecx
calign
.dig3:
if string_bits = 32
	mov	eax, [rsi]
	add	rsi, 4
else
	movzx	eax, word [rsi]
	add	rsi, 2
end if
	cmp	eax, '.'
	je	.dig3done
	cmp	eax, '0'
	jb	.retempty
	cmp	eax, '9'
	ja	.retempty
	sub	eax, '0'
	imul	ecx, ecx, 10
	add	ecx, eax
	sub	edx, 1
	jnz	.dig3
	jmp	.retempty
calign
.dig3done:
	sub	edx, 1
	jz	.retempty
	; third octet is in ecx
	shl	ecx, 16
	or	r8d, ecx
	xor	ecx, ecx
calign
.dig4:
if string_bits = 32
	mov	eax, [rsi]
	add	rsi, 4
else
	movzx	eax, word [rsi]
	add	rsi, 2
end if
	cmp	eax, '0'
	jb	.retempty
	cmp	eax, '9'
	ja	.retempty
	sub	eax, '0'
	imul	ecx, ecx, 10
	add	ecx, eax
	sub	edx, 1
	jnz	.dig4
	; fourth octet is in ecx
	shl	ecx, 24
	or	r8d, ecx
	mov	esi, r8d
	call	maxmind$citycountry_ipv4
	epilog

end if
