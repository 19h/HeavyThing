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
	; mtree_aesxts.inc: an "overlay" such that two mapped objects get used:
	; 1) an anonymous map in plaintext for the mtree itself
	; 2) a file based mapped object that holds the AESXTS version of same.
	; as well as an additional integrity map that holds HMAC(SHA512) for every
	; mtree_nodesize (4kb) block. (HMAC is done on encrypted blocks only, and
	; only serves to verify that the contents are known-good, rather than
	; possible errors however small causing crashes, or other undefined
	; behaviour).
	;
	; Not efficient of course for large maps, but very effective for small
	; and makes it easy to deal with an encrypted disk-based mtree.
	;
	; since the beginning of the object created here is the mtree object,
	; this means that using this object for all mtree calls is fine, and then
	; mtree_aesxts$update needs to be called after the tree gets modified
	; to write out the changed blocks.
	;

if used mtree_aesxts$new | defined include_everything

mtree_aesxts_mtree_ofs = 0
mtree_aesxts_mapped_ofs = mtree_size
mtree_aesxts_imapped_ofs = mtree_aesxts_mapped_ofs + 8
mtree_aesxts_ofs = mtree_aesxts_imapped_ofs + 8

mtree_aesxts_hmac_ofs = mtree_aesxts_ofs + aesxts_size

mtree_aesxts_size = mtree_aesxts_hmac_ofs + hmac_size


	; three arguments: edi == settings flags for mtree, rsi == string filename, rdx == ptr to 128 bytes key material
	; returns new object, or null if underlying file error occurs, or -1 in rax if integrity check failed
falign
mtree_aesxts$new:
	prolog	mtree_aesxts$new
	push	rbx r12 r13 r14 r15
	mov	r12, rdi
	mov	r13, rsi
	mov	r14, rdx
	mov	edi, mtree_aesxts_size
	call	heap$alloc_clear
	mov	rbx, rax
	; first up, initialize our aesxts context:
	lea	rdi, [rax+mtree_aesxts_ofs]
	mov	rsi, r14
	lea	rdx, [r14+32]
	call	aesxts$keys
	; next up, initialize our hmac context:
	lea	rdi, [rbx+mtree_aesxts_hmac_ofs]
	call	hmac$init_sha512
	lea	rdi, [rbx+mtree_aesxts_hmac_ofs]
	lea	rsi, [r14+64]
	mov	edx, 64
	call	hmac$key
	; create a concatenated version of the filename .integrity for our HMAC mapping
	mov	rdi, r13
	mov	rsi, .dotintegrity
	call	string$concat
	mov	r15, rax
	; if our file does not exist, initialize it freshlike
	mov	rdi, r13
	call	file$size
	test	rax, rax
	jz	.freshy
	; otherwise, make certain it is 4k aligned
	test	rax, 4095
	jnz	.freshy
	; make sure our integrity file exists
	mov	rdi, r15
	call	file$size
	test	rax, rax
	jz	.freshy
	; otherwise, make certain it is 64 byte aligned
	test	rax, 63
	jnz	.freshy
	; load it
	mov	edi, 4096
	mov	rsi, r13
	call	mapped$new
	mov	[rbx+mtree_aesxts_mapped_ofs], rax
	; so now, we need a new anonymous mapped object of the same size
	mov	rdi, [rax+mapped_size_ofs]
	call	mapped$new_anon
	; mtree$new_mapped only sets a few pointers, so we do that here ourselves:
	mov	rdx, [rax+mapped_base_ofs]
	mov	[rbx+mtree_settings_ofs], r12
	mov	[rbx+mtree_map_ofs], rax
	mov	[rbx+mtree_base_ofs], rdx
	; copy encrypted map to anon map
	mov	rdi, rdx
	mov	rsi, [rbx+mtree_aesxts_mapped_ofs]
	mov	rdx, [rsi+mapped_size_ofs]
	mov	rsi, [rsi+mapped_base_ofs]
	call	memcpy
	; load our integrity map
	mov	edi, 64
	mov	rsi, r15
	call	mapped$new
	mov	[rbx+mtree_aesxts_imapped_ofs], rax
	; we are done with the string in r15
	mov	rdi, r15
	call	heap$free
	; we are also done with r14, r13, r12
	; first things first, make sure the length of the integrity file
	; is correct in relation to the source map
	mov	r12, [rbx+mtree_base_ofs]
	mov	r13, [rbx+mtree_aesxts_mapped_ofs]
	mov	r14, [rbx+mtree_aesxts_imapped_ofs]
	mov	r13, [r13+mapped_size_ofs]	; size of our source
	mov	r15, [r14+mapped_base_ofs]	; our integrity map's base
	mov	r14, [r14+mapped_size_ofs]	; size of our integrity map
	shr	r13, 6				; (source size / 4096) * 64

	sub	rsp, 64				; space for our transient HMAC(SHA512)

	cmp	r13, r14
	jne	.integrity_fail
	shr	r14, 6				; # of blocks we have to do
	; verify its integrity
calign
.verify:
	lea	rdi, [rbx+mtree_aesxts_hmac_ofs]
	mov	rsi, r12
	mov	edx, 4096
        call    qword [rdi+hmac_macupdate_ofs]
	lea	rdi, [rbx+mtree_aesxts_hmac_ofs]
	mov	rsi, rsp
	call	hmac$final
	; make sure it matches:
	mov	rdi, rsp
	mov	rsi, r15
	mov	edx, 64
	call	memcmp32
	test	eax, eax
	jnz	.integrity_fail
	add	r15, 64
	add	r12, 4096
	sub	r14, 1
	jnz	.verify

	; if we made it to here, all good.
	add	rsp, 64

	; decrypt it in place
	mov	r12, [rbx+mtree_base_ofs]
	mov	r13, [rbx+mtree_aesxts_mapped_ofs]
	mov	r13, [r13+mapped_size_ofs]
	xor	r14d, r14d
	sub	rsp, 16
calign
.decrypt:
	mov	[rsp], r14
	mov	[rsp+8], r14
	lea	rdi, [rbx+mtree_aesxts_ofs]
	mov	rsi, r12
	mov	rdx, rsp
	call	aesxts$decrypt
	add	r12, 4096
	add	r14, 1
	sub	r13, 4096
	jnz	.decrypt
	add	rsp, 16
	mov	rax, rbx
	pop	r15 r14 r13 r12 rbx
	epilog
cleartext .dotintegrity, '.integrity'
calign
.freshy:
	; r13 is our filename, create a normal mapped file first
	mov	edi, mtree_nodesize
	mov	rsi, r13
	call	mapped$new
	; if that returned null, we must also
	test	rax, rax
	jz	.nodeal
	mov	[rbx+mtree_aesxts_mapped_ofs], rax

	mov	edi, 64
	mov	rsi, r15
	call	mapped$new
	test	rax, rax
	jz	.nodeal_imapped
	mov	[rbx+mtree_aesxts_imapped_ofs], rax
	; done with the string in r15
	mov	rdi, r15
	call	heap$free

	; then create a virgin mtree object
	mov	edi, mtree_nodesize
	call	mapped$new_anon
	mov	rdx, [rax+mapped_base_ofs]
	mov	[rbx+mtree_settings_ofs], r12
	mov	[rbx+mtree_map_ofs], rax
	mov	[rbx+mtree_base_ofs], rdx

	; then call update
	mov	rdi, rbx
	call	mtree_aesxts$update

	; done, dusted
	mov	rax, rbx
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.integrity_fail:
	; clear mtree object, teardown both mapped objects, clear our state, return -1
	mov	rdi, [rbx+mtree_map_ofs]
	call	mapped$destroy
	mov	rdi, [rbx+mtree_aesxts_mapped_ofs]
	call	mapped$destroy
	mov	rdi, [rbx+mtree_aesxts_imapped_ofs]
	call	mapped$destroy

	mov	rdi, rbx
	xor	esi, esi
	mov	edx, mtree_aesxts_size
	call	memset32

	mov	rdi, rbx
	call	heap$free

	mov	rax, -1
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.nodeal_imapped:
	mov	rdi, [rbx+mtree_aesxts_mapped_ofs]
	call	mapped$destroy
	; fallthrough to normal nodeal
calign
.nodeal:
	; so here, we dont have any dynamically allocated goods to get rid of
	; but our aes and hmac states are both here, clear them out before we 
	; heap$free ourselves
	mov	rdi, rbx
	xor	esi, esi
	mov	edx, mtree_aesxts_size
	call	memset32

	mov	rdi, rbx
	call	heap$free

	xor	eax, eax
	pop	r15 r14 r13 r12 rbx
	epilog

end if


if used mtree_aesxts$destroy | defined include_everything
	; single argument in rdi: our mtree_aesxts object
falign
mtree_aesxts$destroy:
	prolog	mtree_aesxts$destroy
	push	rbx
	mov	rbx, rdi
	call	mtree_aesxts$update
	mov	rdi, [rbx+mtree_aesxts_imapped_ofs]
	call	mapped$destroy
	mov	rdi, [rbx+mtree_aesxts_mapped_ofs]
	call	mapped$destroy
	mov	rdi, [rbx+mtree_map_ofs]
	call	mapped$destroy
	mov	rdi, rbx
	xor	esi, esi
	mov	edx, mtree_aesxts_size
	call	memset32
	mov	rdi, rbx
	call	heap$free
	pop	rbx
	epilog

end if


if used mtree_aesxts$update | defined include_everything
	; single argument in rdi: our mtree_aesxts object
falign
mtree_aesxts$update:
	prolog	mtree_aesxts$update
	push	rbx r12 r13 r14 r15
	mov	rbx, rdi
	mov	rsi, [rdi+mtree_map_ofs]
	mov	r13, [rdi+mtree_aesxts_mapped_ofs]
	mov	r12, [rdi+mtree_base_ofs]
	mov	r14, [rsi+mapped_size_ofs]
	xor	r15d, r15d
	sub	rsp, 4096 + 16
	; make sure the size of the mapped object is good
	cmp	r14, [r13+mapped_size_ofs]
	je	.sizeokay
	; our map should never shrink
	mov	rdi, r13
	mov	rsi, r14
	call	mapped$growto
	; do the same with our integrity map
	mov	rdi, [rbx+mtree_aesxts_imapped_ofs]
	mov	rsi, r14
	shr	rsi, 6			; (source size / 4096) * 64
	call	mapped$growto
.sizeokay:
	mov	r13, [r13+mapped_base_ofs]
	; rather than make every single page dirty in the file mapping,
	; we encrypt a page at a time, and see if it has been modified
calign
.doit:
	mov	rdi, rsp
	mov	rsi, r12		; plaintext mtree object source
	mov	edx, 4096
	call	memcpy
	mov	[rsp+4096], r15
	mov	[rsp+4096+8], r15
	lea	rdi, [rbx+mtree_aesxts_ofs]
	mov	rsi, rsp
	lea	rdx, [rsp+4096]
	call	aesxts$encrypt
	; compare that to the map at r13
	mov	rdi, rsp
	mov	rsi, r13
	mov	edx, 4096
	call	memcmp32
	test	eax, eax
	jz	.notdirty
	mov	rdi, r13
	mov	rsi, rsp
	mov	edx, 4096
	call	memcpy
	; calculate our integrity hmac for it
	lea	rdi, [rbx+mtree_aesxts_hmac_ofs]
	mov	rsi, rsp
	mov	edx, 4096
	call	qword [rdi+hmac_macupdate_ofs]
	mov	rsi, [rbx+mtree_aesxts_imapped_ofs]
	mov	rdx, r15
	lea	rdi, [rbx+mtree_aesxts_hmac_ofs]
	mov	rsi, [rsi+mapped_base_ofs]
	shl	rdx, 6
	add	rsi, rdx
	call	hmac$final
.notdirty:
	add	r15, 1
	add	r13, 4096
	add	r12, 4096
	sub	r14, 4096
	jnz	.doit
	add	rsp, 4096 + 16
	pop	r15 r14 r13 r12 rbx
	epilog

end if
