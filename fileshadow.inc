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
	;       fileshadow.inc: "read/modify" either mmap-backed or plain memory
	;	backed "file", but keep modifications separate, either also in
	;	memory or file-based as well
	;
	; Burning Purpose: without actually modifying a read-only source, our
	; interface allows for insert, delete, overwrite as a "shadow", such that
	; post-modification calls to seek/read gives the "new version". In addition
	; we allow "commits" to then overwrite the original.
	;


if used fileshadow$new | used fileshadow$new_file | defined include_everything

fileshadow_base_ofs = 0		; our read-only source
fileshadow_size_ofs = 8		; our read-only source size
fileshadow_privmapped_ofs = 16	; if file backed, this is set.
fileshadow_destroycb_ofs = 24	; function to call (if nonzero) before destroy completes
fileshadow_destroycbarg_ofs = 32
fileshadow_permitmods_ofs = 40	; bool
fileshadow_pos_ofs = 48		; current position (not used for _offset functions)
fileshadow_mtree_ofs = 56	; mtree for modifications, null if !permitmods
fileshadow_filesize_ofs = 64	; computed filesize (either read-only source size, or modmap based)
fileshadow_size = 72


fileshadow_modmap_size_ofs = 0
fileshadow_modmap_offset_ofs = 8
fileshadow_modmap_inheap_ofs = 16	; (dword)

fileshadow_modmap_size = 20

end if


if used fileshadow$new | defined include_everything
	; three arguments: rdi == buffer, rsi == length of same, edx == bool allow modifications
falign
fileshadow$new:
	prolog	fileshadow$new
	push	rdi rsi rdx
	mov	edi, fileshadow_size
	call	heap$alloc_clear
	pop	rdx rsi rdi
	mov	[rax+fileshadow_base_ofs], rdi
	mov	[rax+fileshadow_size_ofs], rsi
	mov	[rax+fileshadow_permitmods_ofs], edx
	mov	[rax+fileshadow_filesize_ofs], rsi
	test	edx, edx
	jnz	.modmap
	epilog
calign
.modmap:
	push	rax
	xor	edi, edi
	call	mtree$new_anon
	mov	rdx, rax
	pop	rax
	mov	[rax+fileshadow_mtree_ofs], rdx
	epilog

end if


if used fileshadow$new_file | defined include_everything
	; two arguments: rdi == string filename, esi == bool allow modifications
falign
fileshadow$new_file:
	prolog	fileshadow$new_file
	push	rsi
	xor	esi, esi
	call	privmapped$new
	push	rax
	mov	edi, fileshadow_size
	call	heap$alloc_clear
	pop	rdi rsi
	mov	[rax+fileshadow_permitmods_ofs], esi
	mov	[rax+fileshadow_privmapped_ofs], rdi
	test	rdi, rdi
	jz	.nofile
	mov	rdx, [rdi+privmapped_base_ofs]
	mov	rcx, [rdi+privmapped_size_ofs]
	mov	[rax+fileshadow_base_ofs], rdx
	mov	[rax+fileshadow_size_ofs], rcx
	mov	[rax+fileshadow_filesize_ofs], rcx
	; if allow mods, then create our modmap
	cmp	dword [rax+fileshadow_permitmods_ofs], 0
	jne	.modmap
.nofile:
	epilog
cleartext .shadow, '.shadow'
calign
.modmap:
	push	rbx
	mov	rbx, rax
	; rdi is the privmapped object, which also has the filename:
	mov	rdi, [rdi+privmapped_filename_ofs]
	mov	rsi, .shadow
	call	string$concat
	push	rax
	xor	edi, edi
	mov	rsi, rax
	call	mtree$new
	mov	[rbx+fileshadow_mtree_ofs], rax
	pop	rdi
	call	heap$free
	; recompute our size
	mov	rdi, rbx
	call	fileshadow$sizecalc
	mov	[rbx+fileshadow_filesize_ofs], rax
	mov	rax, rbx
	pop	rbx
	epilog

end if


if used fileshadow$destroy_callback | defined include_everything
	; three arguments: rdi == fileshadow object, rsi == function to call, rdx == argument to pass to it in rdi
	; placeholder only really, set it yourself
falign
fileshadow$destroy_callback:
	prolog	fileshadow$destroy_callback
	mov	[rdi+fileshadow_destroycb_ofs], rsi
	mov	[rdi+fileshadow_destroycbarg_ofs], rdx
	epilog

end if


if used fileshadow$destroy | defined include_everything
	; single argument in rdi: fileshadow object
falign
fileshadow$destroy:
	prolog	fileshadow$destroy
	push	rdi
	mov	rsi, rdi
	cmp	qword [rdi+fileshadow_destroycb_ofs], 0
	je	.nocallback
	mov	rdi, [rdi+fileshadow_destroycbarg_ofs]
	call	qword [rsi+fileshadow_destroycb_ofs]
.nocallback:
	mov	rcx, [rsp]
	cmp	qword [rcx+fileshadow_privmapped_ofs], 0
	je	.noprivmapped
	mov	rdi, [rcx+fileshadow_privmapped_ofs]
	call	privmapped$destroy
.noprivmapped:
	mov	rcx, [rsp]
	cmp	qword [rcx+fileshadow_mtree_ofs], 0
	je	.nomtree
	mov	rdi, [rcx+fileshadow_mtree_ofs]
	call	mtree$destroy
.nomtree:
	pop	rdi
	epilog

end if


if used fileshadow$sync | defined include_everything
	; two arguments: rdi == fileshadow object, esi == bool as to whether to async it or not
	; NOTES: this is a wrapper o'er top of mtree's sync, does nothing if no mtree object
	; exists or the mapping is anonymous (not filebacked)
falign
fileshadow$sync:
	prolog	fileshadow$sync
	mov	rdi, [rdi+fileshadow_mtree_ofs]
	test	rdi, rdi
	jz	.ret
	call	mtree$sync
.ret:
	epilog

end if


if used fileshadow$sizecalc | defined include_everything
	; single argument in rdi: fileshadow object
	; if the modmap is populated, this returns its end offset+size
	; otehrwise, returns our read-only source size
	; this is not normally needed, as we maintain fileshadow_filesize_ofs for mods as we go
	; but initially (on new_file) this is called to figure out the size initially.
falign
fileshadow$sizecalc:
	prolog	fileshadow$sizecalc
	mov	rsi, [rdi+fileshadow_mtree_ofs]
	mov	rax, [rdi+fileshadow_size_ofs]
	test	rsi, rsi
	jnz	.maybe_modmap
	epilog
.maybe_modmap:
	push	rsi rax
	mov	rdi, rsi
	call	mtree$empty
	pop	rcx rdi
	test	eax, eax
	cmovnz	rax, rcx
	jz	.modmap
	epilog
.modmap:
	; rdi == mtree, we need its end size
	; we need its maximum key/val
	sub	rsp, mtree_iter_size + 32
	mov	rsi, rsp
	push	rdi
	call	mtree$maximum
	lea	rdi, [rsp+8]
	call	mtree_iter$key_value
	pop	rdi
	; the offset to the modmap entry is sitting in rdx
	mov	[rsp+mtree_iter_size+24], rax		; save the effective offset rel to the fileshadow
	mov	rsi, rdx				; offset of the modmap entry
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$read
	mov	rax, [rsp+mtree_iter_size+24]
	add	rax, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]
	add	rsp, mtree_iter_size + 32
	epilog

	
end if


if used fileshadow$seek | defined include_everything
	; two arguments: rdi == fileshadow object, rsi == position
	; returns bounded position
falign
fileshadow$seek:
	prolog	fileshadow$seek
	mov	rdx, [rdi+fileshadow_filesize_ofs]
	mov	rax, rsi
	cmp	rsi, rdx
	cmova	rax, rdx
	mov	[rdi+fileshadow_pos_ofs], rax
	epilog

end if


if used fileshadow$read | defined include_everything
	; three arguments: rdi == fileshadow object, rsi == buffer, rdx == bytes to read
	; reads from the current file position, updates current file position, returns # bytes read
	; (a wrapper o'er top of the offset version)
falign
fileshadow$read:
	prolog	fileshadow$read
	push	rdi
	mov	rcx, [rdi+fileshadow_pos_ofs]
	call	fileshadow$read_offset
	pop	rdi
	add	[rdi+fileshadow_pos_ofs], rax
	epilog

end if


if used fileshadow$read_offset | defined include_everything
	; four arguments: rdi == fileshadow object, rsi == buffer, rdx == bytes to read, rcx == offset/position
	; returns # of bytes read in rax
falign
fileshadow$read_offset:
	prolog	fileshadow$read_offset
	xor	eax, eax
	test	rdx, rdx
	jz	.ret
	cmp	rcx, [rdi+fileshadow_filesize_ofs]
	ja	.ret
	; sanity checks complete
	push	rbx r12 r13
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13, rdx
	push	r14 r15
	mov	r14, rcx
	mov	r15, [rbx+fileshadow_mtree_ofs]
	; if there is no mtree, straight from the base underlying it is
	test	r15, r15
	jz	.modmap_empty
	; first case: mtree is empty
	mov	rdi, r15
	call	mtree$empty
	test	eax, eax
	jnz	.modmap_empty

	sub	rsp, mtree_iter_size + 96
	; accumulate # of bytes we have read so far at +32
	mov	qword [rsp+mtree_iter_size+32], 0
calign
.readloop:
	; if the offset is at the end (possible with a loop jump back up to here)
	; all done
	cmp	r14, [rbx+fileshadow_filesize_ofs]
	je	.readloop_done
	; otherwise, get the lower_bound or maximum
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, rsp
	call	mtree$lower_bound
	; if we didn't get a lower_bound, there is no modmap entry >= the one we are after, and since we know it is
	; not out of bounds (.e.g offset of read request is not past the end), then we can safely use the maximum
	test	eax, eax
	jz	.modmap_need_maximum
	; if the key of the iterator != our offset, go back one
	mov	rdi, rsp
	call	mtree_iter$key
	cmp	rax, r14
	je	.modmap_iter_ready
	mov	rdi, rsp
	call	mtree_iter$prev
.modmap_iter_ready:
	; so our iterator is either the last iterator (in which case our offset is >it, but not past the end)
	; or it is the first one >= our offset
	mov	rdi, rsp
	call	mtree_iter$key_value
	; hangon to hte key/val
	mov	[rsp+mtree_iter_size+48], rax		; offset of this modmap block (effective offset of fileshadow proper rel r14)
	mov	[rsp+mtree_iter_size+56], rdx		; heap offset of the modmap record
	; read the modmap record
	mov	rdi, r15
	mov	rsi, rdx
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$read

	; calculate our effective offset into this modmap block (where we ultimately are going to read relative to its modmap_offset)
	; calculate our effective size (which is modmap's size - effective_offset), aka how many bytes remain in this modmap block
	; calculate our to_read (which is min(bytecount, effective_size))

	mov	rax, r14				; our offset/position for this read
	mov	rcx, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]	; the size of this modmap block
	sub	rax, [rsp+mtree_iter_size+48]		; our offset/position - the effective offset of this modmap block == effective offset into this modmap block of our read
	sub	rcx, rax				; effective size == how many bytes remain in this modmap block for our write
	mov	rdx, r13
	cmp	rcx, r13
	cmovb	rdx, rcx				; rdx == min(bytecount to read, effective size remaining in this modmap block)

	; hangon to those values
	mov	[rsp+mtree_iter_size+64], rax		; == effective offset into this modmap block of our read
	mov	[rsp+mtree_iter_size+72], rcx		; effective size remaining in this modmap block for our read
	mov	[rsp+mtree_iter_size+80], rdx		; to_read == how many bytes we can read, aka min(bytecount, effective size remaining)

	; only two possibles: inheap or not.
	cmp	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	je	.readloop_inheap
	; otherwise, this modmap entry is from the underlying source
	mov	rdi, r12
	mov	rsi, [rbx+fileshadow_base_ofs]
	; rdx is already set to a valid to_read
	add	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]	; + the offset
	add	rsi, rax				; + effective offset into this modmap
	call	memcpy
	; update our position and keep going

	mov	rax, [rsp+mtree_iter_size+80]		; to_read
	add	r12, rax
	add	r14, rax
	add	[rsp+mtree_iter_size+32], rax		; cumulative total of how many we've read
	sub	r13, rax
	jnz	.readloop

	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.readloop_inheap:
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]	; offset to read from
	mov	rcx, rdx				; rcx == to_read
	mov	rdx, r12				; destination buffer
	add	rsi, rax				; offset to read from += effective_offset
	call	mtree$read

	; update our position and keep going
	mov	rax, [rsp+mtree_iter_size+80]		; to_read
	add	r12, rax
	add	r14, rax
	add	[rsp+mtree_iter_size+32], rax		; cumulative total of how many we've read
	sub	r13, rax
	jnz	.readloop

	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.modmap_need_maximum:
	mov	rdi, r15
	mov	rsi, rsp
	call	mtree$maximum
	jmp	.modmap_iter_ready
calign
.readloop_done:
	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.modmap_empty:
	; simple case, no modmap/mtree to traverse... dest == r12, source == base+r14, for min(r13, filesize-r14) return for
	mov	rax, [rbx+fileshadow_filesize_ofs]
	mov	rdi, r12
	mov	rsi, [rbx+fileshadow_base_ofs]
	sub	rax, r14
	mov	rdx, r13
	add	rsi, r14			; source = base+r14
	cmp	rdx, rax
	cmova	rdx, rax
	mov	r15, rdx
	call	memcpy
	mov	rax, r15
	pop	r15 r14 r13 r12 rbx
.ret:
	epilog

end if



if used fileshadow$write | defined include_everything
	; three arguments: rdi == fileshadow object, rsi == buffer, rdx == bytes to write
	; writes to the current file position, updates current file position, returns # bytes written
	; (a wrapper o'er top of the offset version)
falign
fileshadow$write:
	prolog	fileshadow$write
	push	rdi
	mov	rcx, [rdi+fileshadow_pos_ofs]
	call	fileshadow$write_offset
	pop	rdi
	add	[rdi+fileshadow_pos_ofs], rax
	epilog

end if



if used fileshadow$write_offset | defined include_everything
	; four arguments: rdi == fileshadow object, rsi == buffer, rdx == bytes to write, rcx == offset/position
	; returns # of bytes written in rax
falign
fileshadow$write_offset:
	prolog	fileshadow$write_offset
	; if modifications are not permitted, return 0
	xor	eax, eax
	cmp	dword [rdi+fileshadow_permitmods_ofs], 0
	je	.ret
	; if for some reason there is no mtree, also bailout
	cmp	qword [rdi+fileshadow_mtree_ofs], 0
	je	.ret
	; if the offset > filesize, return 0 (offset == filesize is okay, then we'll append/expand)
	cmp	rcx, [rdi+fileshadow_filesize_ofs]
	ja	.ret
	test	rdx, rdx		; zero byte write == nothing to do
	jz	.ret
	; sanity checks complete...
	push	rbx r12 r13
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13, rdx
	push	r14 r15
	mov	r14, rcx
	mov	r15, [rbx+fileshadow_mtree_ofs]
	; first case: mtree is empty
	mov	rdi, r15
	call	mtree$empty
	test	eax, eax
	jnz	.modmap_empty

	sub	rsp, mtree_iter_size + 96
	; save our total # of bytes we are writing for our final return
	mov	[rsp+mtree_iter_size+32], r13
calign
.writeloop:
	; simple case: offset is at the end (aka append)
	cmp	r14, [rbx+fileshadow_filesize_ofs]
	je	.modmap_append
	; otherwise, we have five possibilities to deal with, all of which require our lower_bound or maximum
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, rsp
	call	mtree$lower_bound
	; if we didn't get a lower bound, there is no modmap entry >= the one we are after, and since we know it is
	; not out of bounds (e.g. offset of write request is not past the end of the write buffer), then we can safely
	; use the maximum bounds of the modmap...
	test	eax, eax
	jz	.modmap_need_maximum
	; if the key of the iterator != our offset, go back one
	mov	rdi, rsp
	call	mtree_iter$key
	cmp	rax, r14
	je	.modmap_iter_ready
	mov	rdi, rsp
	call	mtree_iter$prev
.modmap_iter_ready:
	; so our iterator is either the last iterator (in which case our offset is >it, but not past the end)
	; or it is the first one >= our offset
	mov	rdi, rsp
	call	mtree_iter$key_value
	; hangon to the key/val
	mov	[rsp+mtree_iter_size+48], rax		; offset of this modmap block (effective offset of fileshadow proper)
	mov	[rsp+mtree_iter_size+56], rdx		; heap offset of the modmap record (where in the heap this modmap record actually lives)
	; read the modmap record
	mov	rdi, r15				; our mtree
	mov	rsi, rdx				; our offset
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$read

	; calculate our effective offset into this modmap (where we ultimately are going to write relative to its modmap_offset)
	; calculate our effective_size (which is modmap's size - effective_offset), aka how many bytes remain in this modmap block
	; calculate our to_write (which is min(bytecount, effective_size))

	mov	rax, r14				; our offset/position for this write block
	mov	rcx, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]	; the size of this modmap block
	sub	rax, [rsp+mtree_iter_size+48]		; our offset/position - the effective offset of this modmap block == effective offset into this modmap block of our write
	sub	rcx, rax				; effective size == how many bytes remain in this modmap block for our write
	mov	rdx, r13
	cmp	rcx, r13
	cmovb	rdx, rcx				; rdx == min(bytecount to write, effective size remaining in this modmap block)

	; hangon to those values
	mov	[rsp+mtree_iter_size+64], rax		; == effective offset into this modmap block of our write
	mov	[rsp+mtree_iter_size+72], rcx		; effective size remaining in this modmap block for our write
	mov	[rsp+mtree_iter_size+80], rdx		; to_write == how many bytes we CAN write, aka min(bytecount, effective size remaining)
	
	; case 1
	; if this modmap block is already in the heap, we can just overwrite it:
	cmp	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	je	.writeloop_inheap

	; if effective offset is zero, and our bytecount is >= our effective size, special case where we can flip the modmap
	test	rax, rax
	jnz	.writeloop_check_case4			; skip to checking case 4, since case 3 is effective offset == 0
	cmp	r13, rcx
	jae	.writeloop_flip_modmap

	; otherwise, effective offset is zero, but our bytecount is < our effective size
	; which means we need to modify the offset and size of the modmap, and increase the lowerbound iterator's key
	; and then create a new one for this write block

	; we know we are not violating the tree order so we can safely move the key forward
	sub	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	add	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r13
	mov	rsi, [rsp+mtree_iter_size+48]		; offset of this modmap block (effective offset of fileshadow proper)
	mov	rdi, rsp
	add	rsi, r13
	call	mtree_iter$set_key

	; save our modmap changes
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; now create a new entries for our block
	mov	rdi, r15
	mov	rsi, r13
	call	mtree$alloc

	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13	; new modmap entry size == bytecount
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax	; offset into the heap of our block
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1	; inheap = true

	; write our block to the heap first
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	mtree$write

	; allocate a new modmap
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that offset:
	mov	[rsp+mtree_iter_size+88], rax

	; write our new modmap there
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; add a new mtree entry for it at offset r14
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, [rsp+mtree_iter_size+88]
	call	mtree$insert

	; bytecount was < effective_size, which means we have no more to write, done, dusted.
	mov	rax, [rsp+mtree_iter_size+32]		; our initial write bytecount
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog

.writeloop_check_case4:
	; if bytecount < effective_size, then worst case of 3 way split
	cmp	r13, rcx
	jb	.writeloop_threeway

	; our final case, effective offset >0, and our bytecount >= the effective size of the remainder of this modmap entry
	; so we have to reduce the size of hte current modmap entry by the effective_size

	; and then add a new entry at our offset for effective_size bytes
	sub	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rcx
	; write that modmap entry back out
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; allocate space for our new one
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+72]	; effective size remaining (past tense)
	call	mtree$alloc
	mov	rcx, [rsp+mtree_iter_size+72]	; effective size remaining (past tense)
	; configure our new modmap record
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rcx
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax	; offset of our new digs
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1	; inheap
	; write our newly allocate goods
	mov	rdi, r15
	mov	rsi, rax			; offset we are writing to
	mov	rdx, r12
	; rcx already set
	call	mtree$write

	; allocate a new modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc

	; save that location:
	mov	[rsp+mtree_iter_size+88], rax
	; write our modmap record
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; last but not least, insert a new mtree key/val for it
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, [rsp+mtree_iter_size+88]
	call	mtree$insert

	; update our position and continue or exit

	mov	rax, [rsp+mtree_iter_size+72]	; effective size (how much we wrote)
	add	r12, rax		; buffer pointer += how many we wrote
	add	r14, rax		; offset/position += how many we wrote
	sub	r13, rax		; bytecount to write -= how many we wrote
	jnz	.writeloop

	mov	rax, [rsp+mtree_iter_size+32]		; our initial write bytecount
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.modmap_need_maximum:
	mov	rdi, r15
	mov	rsi, rsp
	call	mtree$maximum
	jmp	.modmap_iter_ready

calign
.writeloop_inheap:
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	rdx, r12
	mov	rcx, [rsp+mtree_iter_size+80]
	add	rsi, [rsp+mtree_iter_size+64]		; effective offset of our write
	call	mtree$write

	; no new modmap entry is required... so just update our position and be done
	mov	rax, [rsp+mtree_iter_size+80]
	add	r12, rax		; buffer pointer += how many we wrote
	add	r14, rax		; offset/position += how many we wrote
	sub	r13, rax		; bytecount to write -= how many we wrote
	jnz	.writeloop

	mov	rax, [rsp+mtree_iter_size+32]		; our initial write bytecount
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.writeloop_flip_modmap:
	; allocate space for our to_write, since it is min(bytecount, effectivesize) and we knwo bytecount >= effectivesize, this
	; is same as effective size
	mov	rdi, r15
	mov	rsi, rcx
	call	mtree$alloc
	; save that offset into the modmap and flip its inheap status
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1	; set this modmap to inheap
	; write our buffer
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, [rsp+mtree_iter_size+72]
	call	mtree$write
	; write our modified modmap block
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; no new modmap entry is required... so just update our position and be done
	mov	rax, [rsp+mtree_iter_size+80]
	add	r12, rax		; buffer pointer += how many we wrote
	add	r14, rax		; offset/position += how many we wrote
	sub	r13, rax		; bytecount to write -= how many we wrote
	jnz	.writeloop

	mov	rax, [rsp+mtree_iter_size+32]		; our initial write bytecount
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.writeloop_threeway:
	; this is the worst case scenario:
	; the effective offset of our write into this block is >0
	; and the bytecount of our write is less than the remaining bytes in this block after our offset
	;
	; which means we need three modmap entries from this single one and our own.
	;
	; so first up, create the last segment along with a new mtree key/val for it
	; size = modmap.size - effective_offset - bytecount
	; offset = modmap.offset + effective_offset + bytecount
	
	; the fileshadow outer offset is r14+bytecount

	sub	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax	; size -= effective_offset
	sub	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13	; size -= bytecount
	add	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax	; offset += effective_offset
	add	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r13	; offset += bytecount
	; its inheap is already set to 0, so leave it

	; allocate a new modmap spot for that
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc

	; save that spot
	mov	[rsp+mtree_iter_size+88], rax
	; write our modified new modmap
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; create our new mtree key/val for it
	mov	rdi, r15
	mov	rsi, r14	; our actual offset
	mov	rdx, [rsp+mtree_iter_size+88]
	add	rsi, r13	; + bytecount is the offset of this one
	call	mtree$insert

	mov	rax, [rsp+mtree_iter_size+64]	; original effective offset into the block
	; undo our changes to the modmap's offset
	sub	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r13	; offset -= bytecount
	sub	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax ; offset -= effective_offset
	; set its size now to the effective_offset
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax	; size = effective_offset

	; write that one out
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; so now we can create a new spot for our write, set inheap to true
	mov	rdi, r15
	mov	rsi, r13
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13	; size == our bytecount
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax	; offset == our new spot on the heap
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	; write our block
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	mtree$write

	; allocate a new spot for our modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc

	; save that spot
	mov	[rsp+mtree_iter_size+88], rax
	; write our new modmap
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; create our new mtree key/val for it
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, [rsp+mtree_iter_size+88]
	call	mtree$insert

	; done, dusted.
	mov	rax, [rsp+mtree_iter_size+32]		; our initial write bytecount
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.modmap_append:
	; special case here where we are only appending (offset == filesize)
	; create a modmap entry for us, !inheap, key(effective offset) is filesize,
	; inheap, for r13 bytes.
	; first up, allocate heap space for this buffer:
	mov	rdi, r15
	mov	rsi, r13		; # of bytes of this write
	call	mtree$alloc
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	mov	r12, rax		; overwrite buffer passed w/ our new heap offset
	call	mtree$write
	; now we need a modmap entry, inheap for this one
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+24], rax	; hangon to it
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r12		; our inheap alloc offset
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1	; inheap = true
	; now write that
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; now we insert
	mov	rdi, r15
	mov	rsi, r14		; this is same as [rbx+fileshadow_filesize_ofs]
	mov	rdx, [rsp+mtree_iter_size+24]	; our modmap offset
	call	mtree$insert
	; increase our filesize by r13 bytes
	add	[rbx+fileshadow_filesize_ofs], r13
	; done, dusted
	add	rsp, mtree_iter_size + 96
	mov	rax, r13		; # of bytes we wrote
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.modmap_empty:
	; special case here where we have no modmap entries whatsoever, this is our first mod
	; if the offset of our write is >0, we need a not-in-heap modmap entry at 0 for r14 bytes
	sub	rsp, mtree_iter_size + 32
	test	r14, r14
	jz	.modmap_empty_nofirst
	; allocate a modmap entry in the mtree first up
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+24], rax	; hangon to it
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r14
	mov	qword [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], 0	; real offset (!inheap)
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0	; !inheap
	; now write that
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; now insert our effective offset (0) + that modmap offset to our actual mtree
	mov	rdi, r15
	xor	esi, esi
	mov	rdx, [rsp+mtree_iter_size+24]	; offset of our modmap entry
	call	mtree$insert
.modmap_empty_nofirst:
	; now we insert another modmap entry for this write contents, first we alloc a spot for
	; this write's buffer
	mov	rdi, r15
	mov	rsi, r13		; # of bytes of this write
	call	mtree$alloc
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	mov	r12, rax
	call	mtree$write
	; now we need a modmap entry, inheap for this one
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+24], rax	; hangon to it
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r12		; our inheap alloc offset
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1	; inheap = true
	; now write that
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; now insert our effective offset (r14) + that modmap offset to our actual mtree
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, [rsp+mtree_iter_size+24]	; offset of our modmap entry
	call	mtree$insert

	; so, if r14+r13 > the previous filesize, set new filesize, and we are done.
	lea	rdx, [r14+r13]
	cmp	rdx, [rbx+fileshadow_filesize_ofs]
	jae	.modmap_empty_filesize_exit
	; otherwise, there is space left after our write that is !inheap, so we need one final modmap entry
	; its offset is r14+r13, and its length is filesize - that
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	lea	rcx, [r14+r13]
	mov	rdx, [rbx+fileshadow_filesize_ofs]
	sub	rdx, rcx
	mov	[rsp+mtree_iter_size+24], rax	; hangon to it
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rdx	; remaining bytes after our write
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rcx	; offset into the underlying !inheap buffer/file
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0	; inheap = false
	; now write that
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; now insert our effective offset (r14+r13) + that modmap offset to our actual mtree
	mov	rdi, r15
	lea	rsi, [r14+r13]
	mov	rdx, [rsp+mtree_iter_size+24]	; offset of our modmap entry in the heap
	call	mtree$insert
	add	rsp, mtree_iter_size + 32
	mov	rax, r13		; # of bytes we wrote
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.modmap_empty_filesize_exit:
	add	rsp, mtree_iter_size + 32
	mov	[rbx+fileshadow_filesize_ofs], rdx
	mov	rax, r13		; # of bytes we wrote
	pop	r15 r14 r13 r12 rbx
.ret:
	epilog

end if



if used fileshadow$insert | defined include_everything
	; three arguments: rdi == fileshadow object, rsi == buffer, rdx == bytes to insert
	; inserts at the current file position, does not update the current file position, returns # bytes inserted
	; (a wrapper o'er top of the offset version)
falign
fileshadow$insert:
	prolog	fileshadow$insert
	mov	rcx, [rdi+fileshadow_pos_ofs]
	call	fileshadow$insert_offset
	epilog

end if



if used fileshadow$insert_offset | defined include_everything
	; four arguments: rdi == fileshadow object, rsi == buffer, rdx == bytes to insert, rcx == offset/position
	; returns # of bytes inserted in rax
falign
fileshadow$insert_offset:
	prolog	fileshadow$insert_offset
	; if modifications are not permitted, or if for some reason there is no mtree, bailout
	xor	eax, eax
	cmp	dword [rdi+fileshadow_permitmods_ofs], 0
	je	.ret
	cmp	qword [rdi+fileshadow_mtree_ofs], 0
	je	.ret
	; if the offset/position is > filesize, also 0
	cmp	rcx, [rdi+fileshadow_filesize_ofs]
	ja	.ret
	; zero byte insert == also 0
	test	rdx, rdx
	jz	.ret
	; sanity checks complete

	push	rbx r12 r13
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13, rdx
	push	r14 r15
	mov	r14, rcx
	mov	r15, [rbx+fileshadow_mtree_ofs]
	mov	rdi, r15
	call	mtree$empty
	test	eax, eax
	jnz	.modmap_empty

	; if the offset is at the end, aka append, special case as its much simpler
	cmp	r14, [rbx+fileshadow_filesize_ofs]
	je	.modmap_append
	
	sub	rsp, mtree_iter_size + 96

.insert_again:	; jumped to from below if other cases are formed
	; otherwise, we need either the lower_bound of our offset or maximum
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, rsp
	call	mtree$lower_bound
	test	eax, eax
	jz	.modmap_need_maximum
	; if the key of the iterator != our offset, go back one
	mov	rdi, rsp
	call	mtree_iter$key
	cmp	rax, r14
	je	.modmap_iter_ready
	mov	rdi, rsp
	call	mtree_iter$prev
.modmap_iter_ready:
	; so our iterator is either the last iterator (in which case our offset is >it, but not past the end)
	; or it is the first one >= our offset
	mov	rdi, rsp
	call	mtree_iter$key_value
	; hangon to the key/val
	mov	[rsp+mtree_iter_size+48], rax		; offset of this modmap block (relative to the fileshadow proper)
	mov	[rsp+mtree_iter_size+56], rdx		; heap offset of the modmap record
	; read the modmap record
	mov	rdi, r15
	mov	rsi, rdx
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$read

	; calculate the effective offset, effective size
	mov	rax, r14
	mov	rcx, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]
	sub	rax, [rsp+mtree_iter_size+48]		; our offset/pos - effective offset of this modmap block (relative to fileshadow)
	sub	rcx, rax				; effective size == how many bytes remain in this modmap block after our effective offset

	; hangon to those values
	mov	[rsp+mtree_iter_size+64], rax
	mov	[rsp+mtree_iter_size+72], rcx

	; three possibilities:
	; case 1: effective offset is zero (the simple case)
	test	rax, rax
	jz	.insert_case1
	; case 2: this modmap block is not in the heap
	cmp	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0
	je	.insert_case2

	; case 3: this modmap block is in the heap, which means we have to do the same amount of tree manip
	; and basic actions as insert_case2, but we have to _physically_ split the modmap inheap block
	; and create two separate ones from it

	; save the original size of our modmap, and its original offset
	mov	rdx, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]
	mov	r8, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	[rsp+mtree_iter_size+80], rdx
	mov	[rsp+mtree_iter_size+88], r8
	
	; reduce the size of the current modmap to effective_offset
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax

	; we need a reduced copy of the first segment of our inheap modmap
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	rdx, rax
	call	mtree$alloc_clone
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax

	; save the modified modmap
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; create a new modmap with size == effective_size, offset == old offset + old size
	mov	rax, [rsp+mtree_iter_size+64]		; effective offset is the new size of the first segment
	mov	rsi, [rsp+mtree_iter_size+88]		; original inheap offset
	mov	rdx, [rsp+mtree_iter_size+72]		; effective size is what was leftover in the block

	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rdx
	add	rsi, rax
	mov	rdi, r15
	call	mtree$alloc_clone
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax

	; free our original offset
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+88]
	call	mtree$free

	; create a spot for our new modmap
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write our new modmap
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; and finally, we need a new mtree key/val for it
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+48]
	mov	rdx, [rsp+mtree_iter_size+24]
	add	rsi, [rsp+mtree_iter_size+64]
	call	mtree$insert

	; go back to the start, which will result in a fallthrough to case1
	jmp	.insert_again
calign
.insert_case2:
	; effective offset is nonzero, and our modmap record says it is not in the heap

	; save the original size of our modmap
	mov	rdx, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]
	mov	[rsp+mtree_iter_size+80], rdx

	; first, reduce the size of our current modmap to effective_offset
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax
	; save the modified modmap
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; second, create a new modmap with size == effective_size, offset == old offset + old size
	mov	rax, [rsp+mtree_iter_size+64]		; effective offset is the new size of the first segment
	mov	rcx, [rsp+mtree_iter_size+72]		; effective size is what was leftover in the block
	
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rcx	; size of last segment == effective_size
	add	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax	; offset += size of first segment

	; create a spot for our new modmap
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write our new modmap
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; and finally, we need a new mtree key/val for it
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+48]
	mov	rdx, [rsp+mtree_iter_size+24]
	add	rsi, [rsp+mtree_iter_size+64]
	call	mtree$insert

	; go back to the start, which will result in a fallthrough to case1
	; TODO: someday when I am bored, redo the case so it doesn't have to jump back, do the fallthrough instead
	jmp	.insert_again
calign
.insert_case1:
	; simple case, our effective offset for the insert is zero, which means we just bump all entries
	; forward by our bytecount, and then insert a new one
	mov	rdi, rsp		; our iterator loc
	call	mtree_iter$key
	mov	rdi, rsp
	lea	rsi, [rax+r13]		; its new effective offset
	call	mtree_iter$set_key
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jnz	.insert_case1
	; now one for our insert block
	mov	rdi, r15
	mov	rsi, r13
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	; write our insert block first
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	mtree$write
	; make room for our modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; add an mtree key/val
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, [rsp+mtree_iter_size+24]
	call	mtree$insert
	; increase our filesize and return

	add	[rbx+fileshadow_filesize_ofs], r13
	mov	rax, r13
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.modmap_need_maximum:
	mov	rdi, r15
	mov	rsi, rsp
	call	mtree$maximum
	jmp	.modmap_iter_ready

calign
.modmap_append:
	sub	rsp, mtree_iter_size + 32
	mov	rdi, r15
	mov	rsi, r13
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	; write our goods
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	mtree$write
	; make room for our modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; create a new mtree key/val for it
	mov	rdi, r15
	mov	rsi, [rbx+fileshadow_filesize_ofs]
	mov	rdx, [rsp+mtree_iter_size+24]
	call	mtree$insert
	; increase our filesize and return
	add	[rbx+fileshadow_filesize_ofs], r13
	mov	rax, r13
	add	rsp, mtree_iter_size + 32
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.modmap_empty:
	sub	rsp, mtree_iter_size + 32
	; if we are inserting at offset 0, simpler still
	test	r14, r14
	jz	.modmap_empty_isfirst
	
	; else, we have to make 2, possibly 3 modmap records
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r14	; size is our insert offset
	mov	qword [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], 0	; offset into base is 0
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0	; !inheap
	; allocate a spot for our modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; insert a new mtree key/val for it
	mov	rdi, r15
	xor	esi, esi		; effective offset 0
	mov	rdx, [rsp+mtree_iter_size+24]	; the modmap record offset
	call	mtree$insert

	; now our insert block
	mov	rdi, r15
	mov	rsi, r13
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	; write our insert block
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	mtree$write
	; allocate a spot for our modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; insert a new mtree key/val for it
	mov	rdi, r15
	mov	rsi, r14		; effective offset is our insert position
	mov	rdx, [rsp+mtree_iter_size+24]
	call	mtree$insert

	mov	rax, [rbx+fileshadow_filesize_ofs]
	; now, if r14 != old file size, then we have a third to insert
	cmp	r14, [rbx+fileshadow_filesize_ofs]
	je	.modmap_empty_notfirst_nolast
	sub	rax, r14				; rax == filesize - our insert position, aka what is left

	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r14	; offset is our insert position in the base
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0	; !inheap

	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; insert a new mtree key/val for it
	mov	rdi, r15
	lea	rsi, [r13+r14]		; effective offset is our insert pos + our bytecount
	mov	rdx, [rsp+mtree_iter_size+24]
	call	mtree$insert
	; fallthrough to increase size and return
.modmap_empty_notfirst_nolast:
	; increase our filesize and return
	add	[rbx+fileshadow_filesize_ofs], r13
	mov	rax, r13
	add	rsp, mtree_iter_size + 32
	pop	r15 r14 r13 r12 rbx
	epilog

calign
.modmap_empty_isfirst:
	; our insert block is first
	mov	rdi, r15
	mov	rsi, r13
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	; write our insert block
	mov	rdi, r15
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	mtree$write
	; allocate a spot for our modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; insert a new mtree key/val for it
	mov	rdi, r15
	xor	esi, esi		; effective offset 0
	mov	rdx, [rsp+mtree_iter_size+24]	; the modmap record offset
	call	mtree$insert

	; now another insert for the rest of the file
	mov	rax, [rbx+fileshadow_filesize_ofs]
	xor	ecx, ecx
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rcx	; 0 offset
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0	; !inheap
	; allocate a spot for the modmap record
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; save that spot
	mov	[rsp+mtree_iter_size+24], rax
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; insert a new mtree key/val for it
	mov	rdi, r15
	mov	rsi, r13		; effective offset is our insert bytecount
	mov	rdx, [rsp+mtree_iter_size+24]	; the modmap record offset
	call	mtree$insert

	; increase our filesize by our bytecount
	add	[rbx+fileshadow_filesize_ofs], r13
	mov	rax, r13
	add	rsp, mtree_iter_size + 32
	pop	r15 r14 r13 r12 rbx
.ret:
	epilog

end if



if used fileshadow$delete | defined include_everything
	; two arguments: rdi == fileshadow object, rsi == bytes to delete
	; deletes from the current file position, does not update the current file position, returns # bytes deleted
	; (a wrapper o'er top of the offset version)
falign
fileshadow$delete:
	prolog	fileshadow$delete
	mov	rdx, [rdi+fileshadow_pos_ofs]
	call	fileshadow$delete_offset
	epilog

end if



if used fileshadow$delete_offset | defined include_everything
	; three arguments: rdi == fileshadow object, rsi == bytes to delete, rdx == offset/position
	; returns # of bytes deleted in rax
falign
fileshadow$delete_offset:
	prolog	fileshadow$delete_offset
	; if modifications are not permitted, or if there is no mtree, return 0
	xor	eax, eax
	mov	rcx, [rdi+fileshadow_filesize_ofs]
	cmp	dword [rdi+fileshadow_permitmods_ofs], 0
	je	.ret
	cmp	qword [rdi+fileshadow_mtree_ofs], 0
	je	.ret
	; if the offset >= filesize, return 0
	cmp	rdx, rcx
	jae	.ret
	sub	rcx, rdx	; max # of bytes we can delete (filesize - offset/position)
	; zero bytes to delete == zero ret as well
	test	rsi, rsi
	jz	.ret

	; sanity cap the maximum number of bytes we CAN delete (such that you could pass 0xffffffffffffffff in rsi and get returned
	; the number of bytes it really deleted)
	cmp	rsi, rcx
	cmova	rsi, rcx

	push	rbx r13 r14 r15
	mov	rbx, rdi
	mov	r13, rsi
	mov	r14, rdx
	mov	r15, [rdi+fileshadow_mtree_ofs]
	; first case: mtree is empty
	mov	rdi, r15
	call	mtree$empty
	test	eax, eax
	jnz	.modmap_empty

	sub	rsp, mtree_iter_size + 96
	; save our total # of bytes we are deleting for our final return
	mov	[rsp+mtree_iter_size+32], r13
calign
.deleteloop:
	; if the offset is at the end (possible from loop continuation), we are done.
	; (filesize itself will have been modified as the loop progressed)
	cmp	r14, [rbx+fileshadow_filesize_ofs]
	je	.deleteloop_done
	; otherwise, lower_bound or maximum
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, rsp
	call	mtree$lower_bound
	; if we didn't get a lower bound, there is no modmap entry >= the one we are after, and since we know it is
	; not out of bounds (e.g. offset of delete request is not past the end of the goods), then we can safely
	; use the maximum bounds of the modmap...
	test	eax, eax
	jz	.modmap_need_maximum
	; if the key of the iterator != our offset, go back one
	mov	rdi, rsp
	call	mtree_iter$key
	cmp	rax, r14
	je	.modmap_iter_ready
	mov	rdi, rsp
	call	mtree_iter$prev
.modmap_iter_ready:
	; get our iterator's key/value, load up modmap record, and determine our effective offset and bytes remaining
	mov	rdi, rsp
	call	mtree_iter$key_value
	; hangon to the key/val
	mov	[rsp+mtree_iter_size+48], rax		; effective offset of this modmap block (relative to fileshadow)
	mov	[rsp+mtree_iter_size+56], rdx		; heap offset of its modmap record
	; read the modmap record
	mov	rdi, r15
	mov	rsi, rdx
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$read

	; calculate our effective offset into this modmap (where we ultimately are going to delete relative to its modmap_offset)
	; calculate our effective size (which is modmap's size - effective_offset), aka how many bytes remain in this modmap block
	; calculate our to_delete (which is min(bytecount, effective_size))

	mov	rax, r14				; our offset/position for this delete
	mov	rcx, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]	; the size of this modmap block
	sub	rax, [rsp+mtree_iter_size+48]		; our offset position - effective offset == effective offset into this modmap block
	sub	rcx, rax				; effective_size == how many bytes remain in this modmap block (from our effective_offset forward)
	mov	rdx, r13
	cmp	rcx, r13
	cmovb	rdx, rcx				; rdx == min(bytecount to delete, effective size remaining in this modmap block)

	; hangon to those values
	mov	[rsp+mtree_iter_size+64], rax		; == effective offset into this modmap block
	mov	[rsp+mtree_iter_size+72], rcx		; effective size remaining in this modmap block
	mov	[rsp+mtree_iter_size+80], rdx		; to_delete == how many bytes we CAN delete, aka min(bytecount, effective_size)

	xor	r8d, r8d
	mov	r9d, 2
	mov	r10d, 1
	test	rax, rax
	cmovnz	r8d, r9d				; r8d == 0 if effective_offset == 0, else r8d == 2
	xor	r9d, r9d

	cmp	r13, rcx
	cmovb	r9d, r10d
	or	r8d, r9d				; r8d++ if bytecount < effective_size
	mov	r10d, 4
	xor	r9d, r9d

	cmp	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 1
	cmove	r9d, r10d
	or	r8d, r9d				; r8d += 4 if inheap
	
	jmp	qword [r8*8+.delete_dispatch]

calign
.delete_base_full:
	; !inheap, effective_offset == 0, bytecount >= effective_size
	; we can delete this entire modmap entry, and reduce all entries from here forward by effective_size
	; first, free the modmap block
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	call	mtree$free
	; now blast our iterator key/val altogether
	mov	rdi, rsp
	call	mtree_iter$delete
	; get its lower bound again, which will be the next one after the one we just deleted
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, rsp
	call	mtree$lower_bound
	; if that returns false, then there is no additional
	test	eax, eax
	jz	.delete_base_full_skipmod
	; otherwise, walk the remaining spots and decrease by effective_size
calign
.delete_base_full_walk:
	mov	rdi, rsp
	call	mtree_iter$key
	mov	rdi, rsp
	mov	rsi, rax
	sub	rsi, [rsp+mtree_iter_size+72]
	call	mtree_iter$set_key
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jnz	.delete_base_full_walk
.delete_base_full_skipmod:
	mov	rax, [rsp+mtree_iter_size+72]
	sub	[rbx+fileshadow_filesize_ofs], rax
	sub	r13, rax
	jnz	.deleteloop

	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 rbx
	epilog

calign
.delete_base_seg1:
	; !inheap, effective_offset == 0, bytecount < effective_size
	; modify and write our modmap record first, bytecount bytes
	sub	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13	; size of this block -= bytecount
	add	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r13	; offset of the !inheap offset += bytecount
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; decrement our filesize by bytecount
	sub	[rbx+fileshadow_filesize_ofs], r13

	; walk the remaining tree decrementing each key by r13
calign
.delete_base_seg1_walk:
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jz	.delete_base_seg1_done
	mov	rdi, rsp
	call	mtree_iter$key
	mov	rdi, rsp
	mov	rsi, rax
	sub	rsi, r13
	call	mtree_iter$set_key
	jmp	.delete_base_seg1_walk
.delete_base_seg1_done:
	; since bytecount was less than effective_size, we know there is no more to be done
	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 rbx
	epilog

calign
.delete_base_seg2:
	; !inheap, effective_offset >0, bytecount >= effective_size
	; modify and write our modmap record first, effective_size bytes

	mov	rax, [rsp+mtree_iter_size+64]	; effective_offset
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	mov	rax, [rsp+mtree_iter_size+72]	; effective_size

	; decrement filesize by bytecount
	sub	[rbx+fileshadow_filesize_ofs], rax

	; walk the remaining tree decrementing each key by effective_size
calign
.delete_base_seg2_walk:
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jz	.delete_base_seg2_done
	mov	rdi, rsp
	call	mtree_iter$key
	mov	rcx, [rsp+mtree_iter_size+72]	; effective_size
	mov	rdi, rsp
	mov	rsi, rax
	sub	rsi, rcx
	call	mtree_iter$set_key
	jmp	.delete_base_seg2_walk
.delete_base_seg2_done:
	sub	r13, [rsp+mtree_iter_size+72]	; btyecount -= effective_size
	jnz	.deleteloop
	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 rbx
	epilog

calign
.delete_base_mid:
	; !inheap, effective_offset >0, bytecount < effective_size
	; modify and write our current modmap record first
	; set the current one's size == effective_offset, add a new one for the remainder
	mov	rax, [rsp+mtree_iter_size+64]	; effective_offset
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; size of the new one is effective_size - bytecount
	; its key/fileshadow offset is this one's key + effective_offset
	mov	rax, [rsp+mtree_iter_size+64]	; effective_offset
	mov	rcx, [rsp+mtree_iter_size+72]	; effective_size
	add	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], r13		; offset += bytecount
	add	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax		; offset += effective_offset
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rcx		; size = effective_size
	sub	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13		; size -= bytecount
	; allocate a new heap spot for our modmap
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	; hangon to that one
	mov	[rsp+mtree_iter_size+24], rax
	; write our new modmap
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; get the key from our previous iterator
	mov	rdi, rsp
	call	mtree_iter$key
	mov	rcx, [rsp+mtree_iter_size+64]	; effective_offset
	mov	rdi, r15
	lea	rsi, [rax+rcx]			; new key is old key + effective_offset
	mov	rdx, [rsp+mtree_iter_size+24]	; modmap record location
	call	mtree$insert

	; decrement filesize and bytecount, we know there is no more
	sub	[rbx+fileshadow_filesize_ofs], r13
	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 rbx
	epilog
calign
.delete_heap_full:
	; inheap, effective_offset == 0, bytecount >= effective_size
	; same as delete_base_full, only we have to mtree$free the offset first
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	call	mtree$free
	jmp	.delete_base_full

calign
.delete_heap_seg1:
	; inheap, effective_offset == 0, bytecount < effective_size
	; since we can't modify heap based blocks inplace, we need to make a copy of
	; the correct space
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	rdx, [rsp+mtree_iter_size+fileshadow_modmap_size_ofs]
	add	rsi, r13		; offset of the new block += bytecount
	sub	rdx, r13		; size of the new block -= bytecount
	call	mtree$alloc_clone
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	call	mtree$free
	; the rest is the same as heap based, only we don't modify the offset now
	sub	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r13
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; decrement our filesize by bytecount
	sub	[rbx+fileshadow_filesize_ofs], r13

	; walk the remaining tree decrementing each key by r13
calign
.delete_heap_seg1_walk:
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jz	.delete_heap_seg1_done
	mov	rdi, rsp
	call	mtree_iter$key
	mov	rdi, rsp
	mov	rsi, rax
	sub	rsi, r13
	call	mtree_iter$set_key
	jmp	.delete_heap_seg1_walk
.delete_heap_seg1_done:
	; since bytecount was less than effective_size, we know there is no more to be done
	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 rbx
	epilog

calign
.delete_heap_seg2:
	; inheap, effective_offset >0, bytecount >= effective_size
	; since we can't modify heap based blocks inplcae, we need to make a copy of
	; the correct space
	mov	rax, [rsp+mtree_iter_size+64]		; effective_offset
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	rdx, rax				; new size == effective_offset
	call	mtree$alloc_clone
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	call	mtree$free
	; set the new size == effective_offset
	mov	rax, [rsp+mtree_iter_size+64]		; effective_offset
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; our overall size is decreasing by effective_size
	mov	rax, [rsp+mtree_iter_size+72]		; effective_size
	sub	[rbx+fileshadow_filesize_ofs], rax
calign
.delete_heap_seg2_walk:
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jz	.delete_heap_seg2_done
	mov	rdi, rsp
	call	mtree_iter$key
	mov	rcx, [rsp+mtree_iter_size+72]		; effective_size
	mov	rdi, rsp
	mov	rsi, rax
	sub	rsi, rcx
	call	mtree_iter$set_key
	jmp	.delete_heap_seg2_walk
.delete_heap_seg2_done:
	sub	r13, [rsp+mtree_iter_size+72]		; bytecount -= effective_size
	jnz	.deleteloop
	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size+96
	pop	r15 r14 r13 rbx
	epilog
	
calign
.delete_heap_mid:
	; inheap, effective_offset >0, bytecount < effective_size
	; again, since we can't modify heap based blocks inplace, we need to make a
	; copy of the correct space and free the prior, noting here we need TWO copies
	mov	rax, [rsp+mtree_iter_size+64]		; effective_offset == new size
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	mov	rdx, rax
	mov	[rsp+mtree_iter_size+88], rsi		; hangon to the previous one
	call	mtree$alloc_clone
	; set our new offset and size
	mov	rcx, [rsp+mtree_iter_size+64]		; effective_offset == new size
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rcx
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+56]
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; allocate another copy of the end segment
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+88]		; the original segment
	mov	rdx, [rsp+mtree_iter_size+72]		; effective_size
	sub	rdx, r13				; - bytecount is new size
	add	rsi, [rsp+mtree_iter_size+64]		; offset += effective_offset
	add	rsi, r13				; offset += bytecount
	call	mtree$alloc_clone
	mov	rdi, r15
	mov	rsi, [rsp+mtree_iter_size+88]
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rax
	call	mtree$free
	mov	rax, [rsp+mtree_iter_size+72]		; effective_size
	sub	rax, r13				; - bytecount is new size
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rax

	; now we need a new modmap entry
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+24], rax		; hangon to our new spot
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write

	; new we need a new effective offset, whose key is old key + effective offset
	mov	rdi, rsp
	call	mtree_iter$key
	mov	rcx, [rsp+mtree_iter_size+64]		; effective_offset
	mov	rdi, r15
	lea	rsi, [rax+rcx]				; new key is old key + effective_offset
	mov	rdx, [rsp+mtree_iter_size+24]
	call	mtree$insert

	; decrement filesize and bytecount, we know there is no more
	sub	[rbx+fileshadow_filesize_ofs], r13

	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 rbx
	epilog

dalign
.delete_dispatch:
	dq	.delete_base_full, .delete_base_seg1, .delete_base_seg2, .delete_base_mid
	dq	.delete_heap_full, .delete_heap_seg1, .delete_heap_seg2, .delete_heap_mid

calign
.deleteloop_done:
	mov	rax, [rsp+mtree_iter_size+32]
	add	rsp, mtree_iter_size + 96
	pop	r15 r14 r13 rbx
	epilog

calign
.modmap_need_maximum:
	mov	rdi, r15
	mov	rsi, rsp
	call	mtree$maximum
	jmp	.modmap_iter_ready

calign
.modmap_empty:
	; up to 2 modmap entries get created for the underlying base that exclude our "deleted" region
	sub	rsp, mtree_iter_size + 32
	test	r14, r14
	jz	.modmap_empty_nofirst
	; otherwise, we need a !inheap modmap entry at 0 for r14 bytes
	; allocate a modmap entry in the mtree first up
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	mov	[rsp+mtree_iter_size+24], rax	; hangon to it
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], r14
	mov	qword [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], 0	; real offset (!inheap)
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0	; !inheap
	; write it
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; add an mtree key/val for effective offset 0 + that modmap record
	mov	rdi, r15
	xor	esi, esi
	mov	rdx, [rsp+mtree_iter_size+24]
	call	mtree$insert
.modmap_empty_nofirst:
	; if r14+r13 > previous filesize, set new filesize and be done with it
	lea	rdx, [r14+r13]
	cmp	rdx, [rbx+fileshadow_filesize_ofs]
	jae	.modmap_empty_filesize_exit
	; otherwise, there is space left after our deleted segment that is !inheap
	; so we need one more modmap entry
	; its offset is r14+r13, and its length is filesize - that
	mov	rdi, r15
	mov	esi, fileshadow_modmap_size
	call	mtree$alloc
	lea	rcx, [r14+r13]
	mov	rdx, [rbx+fileshadow_filesize_ofs]
	sub	rdx, rcx
	mov	[rsp+mtree_iter_size+24], rax		; hangon to it
	mov	[rsp+mtree_iter_size+fileshadow_modmap_size_ofs], rdx	; remaining bytes after our delete
	mov	[rsp+mtree_iter_size+fileshadow_modmap_offset_ofs], rcx	; offset into the underlying !inheap buffer/file
	mov	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0	; !inheap
	; write that
	mov	rdi, r15
	mov	rsi, rax
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$write
	; insert our delete offset + that modmap record into the mtree
	mov	rdi, r15
	mov	rsi, r14
	mov	rdx, [rsp+mtree_iter_size+24]
	call	mtree$insert
.modmap_empty_filesize_exit:
	; reduce the filesize by our bytecount
	add	rsp, mtree_iter_size + 32
	sub	[rbx+fileshadow_filesize_ofs], r13
	mov	rax, r13
	pop	r15 r14 r13 rbx
.ret:
	epilog

end if


if used fileshadow$commit | defined include_everything
	; two arguments: rdi == fileshadow object, esi == history count
	; NOTES: This does nothing if called on a non-file-backed fileshadow.
	; What this does is write a new file of the shadowed contents, then
	; eliminates the shadow, then renames original file to a version-numbered name
	; and moves our new file back in place.
	; This does not blast the shadow, but does revert it to an empty modmap.
	; returns a bool in eax as to whether we did the deed or not
falign
fileshadow$commit:
	prolog	fileshadow$commit
	cmp	dword [rdi+fileshadow_permitmods_ofs], 0
	je	.nothingtodo
	cmp	qword [rdi+fileshadow_mtree_ofs], 0
	je	.nothingtodo
	cmp	qword [rdi+fileshadow_privmapped_ofs], 0
	je	.nothingtodo
	push	rbx r12 r13 r14 r15
	mov	rbx, rdi
	mov	r12d, esi

	; we make use of mtree's get/setint index 5 to store our revision number
	; get the current revision number + 1
	mov	rdi, [rdi+fileshadow_mtree_ofs]
	mov	esi, 5
	call	mtree$getint
	lea	rdi, [rax+1]
	mov	esi, 16
	call	string$from_unsigned
	mov	r14, rax
	mov	rdi, rax
	mov	esi, 8
	mov	edx, '0'
	call	string$lpad
	mov	rdi, r14
	mov	r14, rax
	call	heap$free
	; r14 is now a zero padded 8 character long hex string

	; create a new filename from that
	mov	rdi, [rbx+fileshadow_privmapped_ofs]
	mov	rdi, [rdi+privmapped_filename_ofs]
	mov	rsi, .dotsep
	call	string$concat
	mov	r13, rax
	mov	rdi, rax
	mov	rsi, r14
	call	string$concat
	mov	rdi, r13
	mov	r13, rax
	call	heap$free
	mov	rdi, r14
	call	heap$free
	; r13 is our filename.xxxxxxxx, r14 got freed

	; so now we have a new filename for it, create a mapped object
	; with a reserve size that matches our computed filesize
	mov	rdi, [rbx+fileshadow_filesize_ofs]
	mov	rsi, r13
	call	mapped$new
	test	rax, rax
	jz	.mappedfail
	mov	r14, rax

	; so now, we can read our entirety into the newly created mapped object
	mov	rdi, rbx
	mov	rsi, [rax+mapped_base_ofs]
	mov	rdx, [rbx+fileshadow_filesize_ofs]
	xor	ecx, ecx
	call	fileshadow$read_offset
	
	; now we can close/destroy our mapped object
	mov	rdi, r14
	call	mapped$destroy

	; now we can clear the existing modmap, leaving the shadow in tact
	mov	rdi, rbx
	call	fileshadow$revert

	; we need a filename of our current revision to rename our file to
	; get the current revision number
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	esi, 5
	call	mtree$getint
	mov	rdi, rax
	mov	esi, 16
	call	string$from_unsigned
	mov	r14, rax
	mov	rdi, rax
	mov	esi, 8
	mov	edx, '0'
	call	string$lpad
	mov	rdi, r14
	mov	r14, rax
	call	heap$free

	; create a new filename from that
	mov	rdi, [rbx+fileshadow_privmapped_ofs]
	mov	rdi, [rdi+privmapped_filename_ofs]
	mov	rsi, .dotsep
	call	string$concat
	mov	r15, rax
	mov	rdi, rax
	mov	rsi, r14
	call	string$concat
	mov	rdi, r15
	mov	r15, rax
	call	heap$free
	mov	rdi, r14
	call	heap$free

	; so now r13 has the next revision filename, r15 has current revision filename
	; make a copy of the base privmapped filename and stick it in r14
	mov	rdi, [rbx+fileshadow_privmapped_ofs]
	mov	rdi, [rdi+privmapped_filename_ofs]
	call	string$copy
	mov	r14, rax

	; destroy our privmapped object
	mov	rdi, [rbx+fileshadow_privmapped_ofs]
	call	privmapped$destroy

	; rename its file to the old revision
	sub	rsp, 16
	mov	rdi, r14
	call	string$utf8_length
	lea	rdi, [rax+8]
	call	heap$alloc
	mov	[rsp], rax
	mov	rdi, r14
	mov	rsi, rax
	call	string$to_utf8
	mov	rcx, [rsp]
	mov	byte [rcx+rax], 0

	mov	rdi, r15
	call	string$utf8_length
	lea	rdi, [rax+8]
	call	heap$alloc
	mov	[rsp+8], rax
	mov	rdi, r15
	mov	rsi, rax
	call	string$to_utf8
	mov	rcx, [rsp+8]
	mov	byte [rcx+rax], 0

	mov	eax, syscall_rename
	mov	rdi, [rsp]
	mov	rsi, [rsp+8]
	syscall
	
	; rename next revision filename to r14 filename
	mov	rdi, [rsp+8]
	call	heap$free
	
	mov	rdi, r13
	call	string$utf8_length
	lea	rdi, [rax+8]
	call	heap$alloc
	mov	[rsp+8], rax
	mov	rdi, r13
	mov	rsi, rax
	call	string$to_utf8
	mov	rcx, [rsp+8]
	mov	byte [rcx+rax], 0

	mov	eax, syscall_rename
	mov	rdi, [rsp+8]
	mov	rsi, [rsp]
	syscall

	; done with both utf8 buffers:
	pop	rdi
	call	heap$free
	pop	rdi
	call	heap$free

	; so now, we can re-initialize our privmapped object
	mov	rdi, r14
	xor	esi, esi
	call	privmapped$new
	mov	[rbx+fileshadow_privmapped_ofs], rax
	mov	rcx, [rax+privmapped_base_ofs]
	mov	rdx, [rax+privmapped_size_ofs]
	mov	[rbx+fileshadow_base_ofs], rcx
	mov	[rbx+fileshadow_size_ofs], rdx
	mov	rdi, rbx
	call	fileshadow$sizecalc
	mov	[rbx+fileshadow_filesize_ofs], rax
	
	; we are done with our 2 strings r13 and r15
	mov	rdi, r13
	call	heap$free
	mov	rdi, r15
	call	heap$free

	; we need to increment our revision number
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	esi, 5
	call	mtree$getint
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	esi, 5
	lea	rdx, [rax+1]
	call	mtree$setint
	

	; last but not least, deal with our revision history
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	esi, 5
	call	mtree$getint
	mov	rdi, rax
	sub	rdi, r12
	sub	rdi, 1
	mov	esi, 16
	call	string$from_unsigned
	mov	r13, rax
	mov	rdi, rax
	mov	esi, 8
	mov	edx, '0'
	call	string$lpad
	mov	rdi, r13
	mov	r13, rax
	call	heap$free
	; so r13 is now an 8 character long hex string
	; r14 is still a copy of the original string

	; create a new filename from that
	mov	rdi, r14
	mov	rsi, .dotsep
	call	string$concat
	mov	r15, rax
	mov	rdi, rax
	mov	rsi, r13
	call	string$concat
	mov	rdi, r15
	mov	r15, rax
	call	heap$free
	mov	rdi, r13
	call	heap$free

	; so r15 now has our history count filename
	; r14 is still a string$copy of the original filename
	; r13 is clear, r12 is no longer needed

	mov	rdi, r15
	call	string$utf8_length
	lea	rdi, [rax+8]
	call	heap$alloc
	mov	r13, rax
	mov	rdi, r15
	mov	rsi, rax
	call	string$to_utf8
	mov	byte [r13+rax], 0

	mov	eax, syscall_unlink
	mov	rdi, r13
	syscall
	
	mov	rdi, r13
	call	heap$free
	mov	rdi, r14
	call	heap$free
	mov	rdi, r15
	call	heap$free

	pop	r15 r14 r13 r12 rbx
.nothingtodo:
	mov	eax, 1		; successful result even if we didn't do anything
	epilog
calign
.mappedfail:
	; free the string we created, and leave everything else alone
	mov	rdi, r13
	call	heap$free
	xor	eax, eax
	pop	r15 r14 r13 r12 rbx
	epilog
cleartext .dotsep, '.'

end if


if used fileshadow$revert | defined include_everything
	; single argument in rdi: fileshadow object
	; NOTE: this function leaves the shadow in tact (useful if you are nesting other trees inside it, or whatever).
	; but deletes one-by-one the modification mtree and all associated modmap records
	; (unlike reset, which blasts it entirely)
falign
fileshadow$revert:
	prolog	fileshadow$revert
	cmp	dword [rdi+fileshadow_permitmods_ofs], 0
	je	.nothingtodo
	cmp	qword [rdi+fileshadow_mtree_ofs], 0
	je	.nothingtodo
	push	rbx r12 r13
	mov	rbx, rdi
	sub	rsp, mtree_iter_size+32
calign
.deleteloop:
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	call	mtree$empty
	test	eax, eax
	jnz	.deleteloop_done
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	rsi, rsp
	call	mtree$minimum
	; load its key/value
	mov	rdi, rsp
	call	mtree_iter$key_value
	mov	r12, rdx		; save its modmap record offset
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	rsi, rdx
	lea	rdx, [rsp+mtree_iter_size]
	mov	ecx, fileshadow_modmap_size
	call	mtree$read
	; regardless of whether this one is in the heap or not, we can free it
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	rsi, r12
	call	mtree$free
	; we can also go ahead and delete the iterator
	mov	rdi, rsp
	call	mtree_iter$delete
	; if the modmap record is in the heap, we have more work to do
	cmp	dword [rsp+mtree_iter_size+fileshadow_modmap_inheap_ofs], 0
	je	.deleteloop
	; otherwise, it is in hte heap, so free its offset
	mov	rdi, [rbx+fileshadow_mtree_ofs]
	mov	rsi, [rsp+mtree_iter_size+fileshadow_modmap_offset_ofs]
	call	mtree$free
	jmp	.deleteloop
calign
.deleteloop_done:
	add	rsp, mtree_iter_size+32
	; recalculate our filesize before we are done
	mov	rdi, rbx
	call	fileshadow$sizecalc
	mov	[rbx+fileshadow_filesize_ofs], rax
	pop	r13 r12 rbx
.nothingtodo:
	epilog

end if


if used fileshadow$reset | defined include_everything
	; single argument in rdi: fileshadow object
	; NOTE: this function _blasts_ the shadow object entirely (and as a result is much faster than revert)
	; (unlike revert, which does it nicely and leaves the shadow object in tact)
falign
fileshadow$reset:
	prolog	fileshadow$reset
	cmp	dword [rdi+fileshadow_permitmods_ofs], 0
	je	.nothingtodo
	cmp	qword [rdi+fileshadow_mtree_ofs], 0
	je	.nothingtodo
	push	rbx r12 r13
	mov	rbx, rdi
	mov	rdi, [rdi+fileshadow_mtree_ofs]
	call	mtree$destroy
	; reconstruct our filename
	mov	rdi, [rbx+fileshadow_privmapped_ofs]
	mov	rdi, [rdi+privmapped_filename_ofs]
	mov	rsi, .shadow
	call	string$concat
	mov	r12, rax
	; unlink the physical filename
	mov	rdi, rax
	call	string$utf8_length
	lea	rdi, [rax+8]
	call	heap$alloc
	mov	r13, rax
	mov	rdi, r12
	mov	rsi, rax
	call	string$to_utf8
	mov	byte [r13+rax], 0
	mov	eax, syscall_unlink
	mov	rdi, r13
	syscall
	mov	rdi, r13
	call	heap$free
	; reconstruct a new mtree with the same filename
	xor	edi, edi
	mov	rsi, r12
	call	mtree$new
	mov	[rbx+fileshadow_mtree_ofs], rax
	mov	rdi, r12
	call	heap$free
	; recompute our size
	mov	rdi, rbx
	call	fileshadow$sizecalc
	mov	[rbx+fileshadow_filesize_ofs], rax
	; done, dusted.
	pop	r13 r12 rbx
.nothingtodo:
	epilog
cleartext .shadow, '.shadow'

end if
