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
	;	mtree.inc: a u64/u64 multikey per node tree with mapped backing.
	;
	;	Not the fastest implementation of a BST, but was easy to modify
	;	to accomplish what I had in mind... time permitting will transcode
	;	the proper btree implementation for this instead.
	;
	;	This code is a multiple-key-per-node left-leaning rbtree. At its
	;	worst case (sequential inserts), half of the nodes end up empty
	;	and thus we waste 16 bytes per key/val, for a total of 32 bytes ea
	;	for each key/val at the worst. This is perfectly acceptable,
	;	in that for a normal single-key/val-per-node rbtree, the minimum
	;	use would be that anyway. In the best case (non-uniform inserts),
	;	this does not occur, and either way, since it operates on 4k pages
	;	it is much more disk friendly (with _way_ fewer tree operations)
	;	than a single key/node tree and that was kinda the point.
	;
	;	Space is cheap, speed is not (especially when on spinning media)
	;	As usual, YMMV.
	;
	;	NOTE: since we "waste" a bunch of space in the first page for
	;	the persistent tree information, we allow up to (mtree_nodesize / 48)
	;	different trees inside the same space.
	;


if used mtree$new_anon | used mtree$new_cstr | used mtree$new | used mtree_aesxts$new | defined include_everything

	; nodesize must be aligned 16, should be aligned pagesize
mtree_nodesize = 4096
mtree_item_max = (mtree_nodesize - 64) / 16


	; number of pages to allocate each time we grow our map
mtree_grow_pagecount = 1


; flags for the bool options
mtree_multi = 1				; allow duplicates
mtree_counting = 2			; count items/nodes


; offsets for our heap$alloc'd mtree object itself
mtree_settings_ofs = 0			; settings (or'd together from above options)
mtree_map_ofs = 8			; mapped object, either anon or filebased
mtree_base_ofs = 16			; copy of the mapped_base_ofs pointer
mtree_current_root_ofs = 24		; offset into the first page of the root info
					; .... this allows multiple separate trees inside
					; the same underlying mapped file/segment.
					; see mtree$setroot

mtree_size = 32


; offsets into the first 4kb page of the underlying mapped object: (rest of first 4k page is wasted)
mtree_root_ofs = 0			; offset of the root node (or 0 for none)
mtree_count_ofs = 8			; offset to the item count
mtree_nodecount_ofs = 16		; offset to the node count
mtree_freecount_ofs = 20		; offset to the free node count
mtree_free_ofs = 24			; offset to the first free node
mtree_first_ofs = 32			; offset to the first node (sort order)
mtree_last_ofs = 40			; offset to the last node (sort order)

mtree_root_size = 48			; size of the info required per tree entry



mtree_maxroot = (mtree_nodesize / mtree_root_size) - 1



; offsets into each node/page:
mtree_left_ofs = 0
mtree_right_ofs = 8
mtree_items_ofs = 16
mtree_flags_ofs = 24
mtree_next_ofs = 32
mtree_prev_ofs = 40
	; note: bytes between 48...64 are wasted space, because we want split_node to be able to do even item counts for new nodes
mtree_firstkey_ofs = 64


end if


if used mtree$new_anon | defined include_everything
	; single argument in edi: settings flags
	; returns new mtree object in rax
falign
mtree$new_anon:
	prolog	mtree$new_anon
	push	rdi
	mov	edi, 1048576 * 2048	; 2GB initial anon size (to avoid remaps up to that point, we shrink it straight back down)
	call	mapped$new_anon
	mov	rsi, [rsp+8]		; page size
	push	rax
	mov	rdi, rax
	add	rsi, mtree_nodesize
	call	mapped$shrinkto
	; let mtree$new_mapped deal with the common rest of it
	pop	rdi rsi
	call	mtree$new_mapped
	epilog

end if


if used mtree$new_cstr | defined include_everything
	; two arguments: edi == settings flags, rsi == null terminated latin1 filename
	; returns new mtree object in rax, or NULL if underlying file error
falign
mtree$new_cstr:
	prolog	mtree$new_cstr
	push	rdi
	mov	edi, mtree_nodesize
	call	mapped$new_cstr
	pop	rsi
	test	rax, rax
	jz	.error
	mov	rdi, rax
	call	mtree$new_mapped
.error:
	epilog

end if

if used mtree$new | defined include_everything
	; two arguments: edi == settings flags, rsi == string filename
	; returns new mtree object in rax, or NULL if underlying file error
falign
mtree$new:
	prolog	mtree$new
	push	rdi
	mov	edi, mtree_nodesize
	call	mapped$new
	pop	rsi
	test	rax, rax
	jz	.error
	mov	rdi, rax
	call	mtree$new_mapped
.error:
	epilog

end if

if used mtree$new_mapped | defined include_everything
	; two arguments: rdi == mapped object, esi == settings flags
	; returns a mtree object in rax
falign
mtree$new_mapped:
	prolog	mtree$new_mapped
	mov	rdx, [rdi+mapped_base_ofs]
	push	rdi rsi rdx
	mov	edi, mtree_size
	call	heap$alloc
	pop	rdx rsi rdi
	xor	ecx, ecx
	mov	[rax+mtree_settings_ofs], rsi
	mov	[rax+mtree_map_ofs], rdi
	mov	[rax+mtree_base_ofs], rdx
	mov	[rax+mtree_current_root_ofs], rcx
	epilog

end if


if used mtree$destroy | defined include_everything
	; single argument in rdi: mtree object
falign
mtree$destroy:
	prolog	mtree$destroy
	push	rdi
	mov	rdi, [rdi+mtree_map_ofs]
	call	mapped$destroy
	pop	rdi
	call	heap$free
	epilog

end if


if used mtree$empty | defined include_everything
	; single argument in rdi: mtree object
	; returns bool in eax as to whether we are empty or not
falign
mtree$empty:
	prolog	mtree$empty
	mov	rsi, [rdi+mtree_base_ofs]
	mov	rdx, [rdi+mtree_current_root_ofs]
	xor	eax, eax
	mov	ecx, 1
	cmp	qword [rsi+rdx+mtree_root_ofs], 0
	cmove	eax, ecx
	epilog

end if


if used mtree$sync | defined include_everything
	; two arguments: rdi == mtree object, esi == bool for whether to do it async or not
	; (convenience wrapper over mapped$sync)
falign
mtree$sync:
	prolog	mtree$sync
	mov	rdi, [rdi+mtree_map_ofs]
	call	mapped$sync
	epilog

end if



if used mtree$setroot | defined include_everything
	; two arguments: rdi == mtree object, esi == index number to use as current root
	; returns bool on success, false if index too big
	; NOTE: all subsequent calls will use this tree info from the first page
	; and this allows multiple wholly separate trees inside the same mapped file/segment.
falign
mtree$setroot:
	prolog	mtree$setroot
	xor	eax, eax
	mov	edx, mtree_nodesize / mtree_root_size
	cmp	esi, mtree_nodesize / mtree_root_size
	jb	.yep
	epilog
.yep:
	xor	edx, edx
	mov	eax, mtree_root_size
	mul	esi
	mov	[rdi+mtree_current_root_ofs], eax
	mov	eax, 1
	epilog

end if


if used mtree$insert | defined include_everything
	; three arguments: rdi == mtree object, rsi == key, rdx == value
	; returns bool in eax as to whether we succeeded or not (multi or not)
falign
mtree$insert:
	prolog	mtree$insert
	push	rbx r12 r13 r14 r15
	mov	r15, [rdi+mtree_current_root_ofs]
	mov	r14, [rdi+mtree_base_ofs]
	mov	rbx, rdi
	mov	r12, rsi
	mov	rdi, [r14+r15+mtree_root_ofs]
	mov	r13, rdx
	test	rdi, rdi
	jz	.newroot
	; find the node we belong in first
calign
.locate_node:
	mov	r8d, [r14+rdi+mtree_items_ofs]
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	sub	r8d, 1
	lea	rsi, [r14+rdi+mtree_firstkey_ofs]
	mov	r10d, r8d			; hi/items-1
	test	rdx, rdx
	jz	.locate_node_checkright
	cmp	r12, [rsi]
	cmovbe	rdi, rdx
	jbe	.locate_node
	; jb	.locate_node_goleft		; left if key < rdi.keys[0]
	; je	.node_found			; if key == rdi.keys[0], this node will do
.locate_node_checkright:
	test	rcx, rcx
	jz	.node_found
	shl	r8d, 4
	cmp	r12, [rsi+r8]			; right if key > rdi.keys[rdi.itemcount-1]
	jbe	.node_found
	; otherwise, travel right
	mov	rdi, rcx
	jmp	.locate_node
calign
.node_found:
	; find the actual spot
	xor	r8d, r8d			; lo
	test	r10d, r10d			; hi
	jz	.node_found_highcheck
calign
.node_found_search:
	lea	edx, [r8d+r10d]
	shr	edx, 1				; mid = (lo+hi) >> 1
	mov	eax, edx
	lea	ecx, [edx+1]			; mid+1
	lea	r11d, [edx-1]			; mid-1
	shl	eax, 4
	cmp	r12, [rsi+rax]
	cmovbe	r10d, r11d
	cmova	r8d, ecx
	cmp	r8d, r10d
	jl	.node_found_search
.node_found_highcheck:
	mov	eax, r10d
	lea	ecx, [r10d+1]
	xor	r8d, r8d
	cmp	r10d, 0
	cmovl	eax, ecx
	jl	.node_found_proceed
	shl	eax, 4

	cmp	[rsi+rax], r12
	cmovb	eax, ecx
	cmovae	eax, r10d
.node_found_proceed:
	; if eax == item count, no memmove required
	mov	edx, [r14+rdi+mtree_items_ofs]
	cmp	eax, edx
	je	.add_end

	sub	edx, eax
	shl	eax, 4
	; if the key at rax is the same as the key we are inserting, make sure we allow duplicates
	cmp	r12, [rsi+rax]
	je	.node_found_proceed_dupcheck
.node_found_proceed_notdup:
	push	rdi rsi rax
if defined original_memmove
	; for a large set random insert, ~20 to 25% of time is spent here:
	shl	edx, 4
	lea	rdi, [rsi+rax+16]
	lea	rsi, [rsi+rax]
	call	memmove
else
	lea	rdi, [rsi+rax+16]
	lea	rsi, [rsi+rax]
	mov	ecx, edx
	shl	edx, 4
	add	rdi, rdx
	add	rsi, rdx
	test	ecx, 1
	jnz	.node_found_proceed_oddmove
	; otherwise, it is even
calign
.node_found_proceed_evenmove:
	sub	rsi, 32
	sub	rdi, 32
	movaps	xmm0, [rsi]
	movaps	xmm1, [rsi+16]
	movaps	[rdi], xmm0
	movaps	[rdi+16], xmm1
	sub	ecx, 2
	jnz	.node_found_proceed_evenmove
.node_found_proceed_movedone:
end if
	pop	rax rsi rdi
	add	dword [r14+rdi+mtree_items_ofs], 1
	mov	[rsi+rax], r12
	mov	[rsi+rax+8], r13
	cmp	dword [r14+rdi+mtree_items_ofs], mtree_item_max
	je	.split_node
	; otherwise, done, dusted.
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jnz	.add_counter
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
calign
.node_found_proceed_oddmove:
	sub	rsi, 16
	sub	rdi, 16
	movaps	xmm0, [rsi]
	movaps	[rdi], xmm0
	sub	ecx, 1
	jnz	.node_found_proceed_evenmove
	jmp	.node_found_proceed_movedone
calign
.node_found_proceed_dupcheck:
	test	dword [rbx+mtree_settings_ofs], mtree_multi
	jz	.falseret
	jmp	.node_found_proceed_notdup
calign
.add_end:
	shl	eax, 4
	add	dword [r14+rdi+mtree_items_ofs], 1
	mov	[rsi+rax], r12
	mov	[rsi+rax+8], r13
	cmp	dword [r14+rdi+mtree_items_ofs], mtree_item_max
	je	.split_node
	; otherwise, done, dusted.
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jnz	.add_counter
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
calign
.add_counter:
	add	qword [r14+r15+mtree_count_ofs], 1
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
calign
.split_node:
	; our key/val already got added, so we are free to repurpose r12/r13
	mov	r12, rdi
	call	.newnode
	mov	r13, rax

	lea	rdi, [r14+r13+mtree_firstkey_ofs]
	lea	rsi, [r14+r12+mtree_firstkey_ofs+((mtree_item_max shr 1) * 16)]
	mov	edx, ((mtree_item_max shr 1) * 16)
	call	memcpy
	mov	dword [r14+r13+mtree_items_ofs], mtree_item_max shr 1
	mov	dword [r14+r12+mtree_items_ofs], mtree_item_max shr 1

	; so now, r12 is the lower half, r13 is the upper half

	; since we know these are in-order, we can deal with our sort order linked list here prior to tree linking r13 into the mix

	mov	rdx, [r14+r12+mtree_next_ofs]	; lower half's next item
	mov	[r14+r13+mtree_prev_ofs], r12	; upper half's prev == lower half
	mov	[r14+r13+mtree_next_ofs], rdx	; upper half's next == old lower half's next
	mov	[r14+r12+mtree_next_ofs], r13	; lower half's next == upper half
	; if rdx is zero, we need to setlast, otherwise, set its new prev to upper half
	test	rdx, rdx
	jz	.split_node_setlast
	mov	[r14+rdx+mtree_prev_ofs], r13	; old lower half's next->prev = upper half

	mov	rdi, r13
	call	.link

	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jnz	.split_node_counting
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
calign
.split_node_setlast:
	mov	[r14+r15+mtree_last_ofs], r13	; last = upper half

	mov	rdi, r13
	call	.link

	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jnz	.split_node_counting
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
calign
.split_node_counting:
	add	qword [r14+r15+mtree_count_ofs], 1
	add	dword [r14+r15+mtree_nodecount_ofs], 1
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
calign
.newroot:
	call	.newnode
	mov	ecx, 1
	xor	r8d, r8d
	mov	[r14+rax+mtree_items_ofs], rcx
	mov	[r14+rax+mtree_firstkey_ofs], r12
	mov	[r14+rax+mtree_firstkey_ofs+8], r13

	; this only gets called when there was no root node, so we can set first/last
	mov	[r14+r15+mtree_first_ofs], rax
	mov	[r14+r15+mtree_last_ofs], rax
	; as well as set its next/prev to 0
	mov	[r14+rax+mtree_next_ofs], r8
	mov	[r14+rax+mtree_prev_ofs], r8

	mov	rdi, rax
	call	.link

	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jnz	.newroot_counter
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
calign
.newroot_counter:
	mov	qword [r14+r15+mtree_count_ofs], 1
	mov	dword [r14+r15+mtree_nodecount_ofs], 1
	pop	r15 r14 r13 r12 rbx
	mov	eax, 1
	epilog
falign
.newnode:
	; note re: multiple roots, we maintain ONE freelist (and only use the first one for however many trees are really here)
	cmp	qword [r14+mtree_free_ofs], 0
	jne	.newnode_fromfreelist
	; else, expand our map, set our new base, update r14
if mtree_grow_pagecount = 1
	mov	rdi, [rbx+mtree_map_ofs]
	mov	esi, mtree_nodesize
	call	mapped$grow
	pxor	xmm0, xmm0
	mov	rdi, [rbx+mtree_map_ofs]
	mov	rsi, [rdi+mapped_base_ofs]
	movaps	[rax], xmm0			; clear left/right
	sub	rax, rsi
	mov	[rbx+mtree_base_ofs], rsi
	mov	r14, rsi
else
	mov	rdi, [rbx+mtree_map_ofs]
	mov	esi, mtree_nodesize * mtree_grow_pagecount
	call	mapped$grow
	pxor	xmm0, xmm0
	mov	rdi, [rbx+mtree_map_ofs]
	mov	rsi, [rdi+mapped_base_ofs]
	movaps	[rax], xmm0
	sub	rax, rsi
	mov	[rbx+mtree_base_ofs], rsi
	mov	r14, rsi
	; rax is the offset of the first page we just allocated
	; add the remaining pages
	lea	rcx, [rax+(mtree_nodesize * (mtree_grow_pagecount - 1))]
	repeat (mtree_grow_pagecount - 1)
		mov	rdx, [rsi+mtree_free_ofs]
		mov	[rsi+rcx], rdx
		mov	[rsi+mtree_free_ofs], rcx
		sub	rcx, mtree_nodesize
	end repeat
end if
	ret
calign
.newnode_fromfreelist:
	pxor	xmm0, xmm0
	mov	rax, [r14+mtree_free_ofs]
	mov	rcx, [r14+rax]
	mov	[r14+mtree_free_ofs], rcx
	movaps	[r14+rax], xmm0			; clear left/right
	ret
calign
.falseret:
	xor	eax, eax
	pop	r15 r14 r13 r12 rbx
	epilog

falign
.link:
	; rbx already == our tree object, rdi == node we are linking into the tree, r15 == root node offset
	mov	rsi, rdi
	push	rbx r12 r13
	mov	rbx, [rbx+mtree_base_ofs]
	mov	r12, rdi
	mov	rdi, [rbx+r15+mtree_root_ofs]
	mov	r8d, [rbx+rsi+mtree_items_ofs]
	push	r14 rdi
	sub	r8d, 1
	mov	dword [rbx+rsi+mtree_flags_ofs], 1
	shl	r8d, 4
	mov	r13, [rbx+rsi+mtree_firstkey_ofs]	; smallest key from the node we are linking
	add	r8, rsi
	mov	r14, [rbx+r8+mtree_firstkey_ofs]	; largest key from the node we are linking
	call	.recurse
	pop	rdi
	cmp	rdi, rax
	jne	.link_newroot
	cmp	dword [rbx+rdi+mtree_flags_ofs], 0
	jne	.rootflags
	pop	r14 r13 r12 rbx
	ret
calign
.link_newroot:
	mov	[rbx+r15+mtree_root_ofs], rax
	mov	dword [rbx+rax+mtree_flags_ofs], 0
	pop	r14 r13 r12 rbx
	ret
calign
.rootflags:
	mov	dword [rbx+rdi+mtree_flags_ofs], 0
	pop	r14 r13 r12 rbx
	ret
falign
.recurse:
	mov	eax, [rbx+rdi+mtree_items_ofs]
	mov	rdx, [rbx+rdi+mtree_left_ofs]
	sub	eax, 1
	mov	rcx, [rbx+rdi+mtree_right_ofs]
	shl	eax, 4
	add	rax, rdi
	test	rdi, rdi
	jz	.recurse_r12ret

	cmp	r14, [rbx+rdi+mtree_firstkey_ofs]
	jbe	.recurse_left
		; had that as just a plain jb before, but for bunches of nodes
		; with the same exact key, this would fallthrough

	cmp	r13, [rbx+rax+mtree_firstkey_ofs]
	jae	.recurse_right
		; same with this, had ja before, but half-full node with largest
		; key all same == the e
	; should not be reached, TODO: that means the above comparison is unnecessary, no?
	breakpoint
calign
.recurse_left:
	push	rdi
	mov	rdi, rdx	; left
	call	.recurse
	pop	rdi
	mov	rdx, [rbx+rdi+mtree_left_ofs]
	mov	rcx, [rbx+rdi+mtree_right_ofs]
	cmp	rdx, rax
	je	.recurse_common
	mov	[rbx+rdi+mtree_left_ofs], rax
	mov	rdx, rax
.recurse_common:
	test	rcx, rcx
	jz	.recurse_common_check2
	cmp	dword [rbx+rcx+mtree_flags_ofs], 1
	jne	.recurse_common_check2
	test	rdx, rdx
	jz	.recurse_common_case1
	cmp	dword [rbx+rdx+mtree_flags_ofs], 0
	jne	.recurse_common_check2
.recurse_common_case1:
	; confusing bit of gear here, rol(rdi), with rdx == rdi.left, rcx == rdi.right
	mov	eax, [rbx+rdi+mtree_flags_ofs]
	mov	r8, [rbx+rcx+mtree_left_ofs]	; right node's left
	mov	[rbx+rdi+mtree_right_ofs], r8
	mov	[rbx+rcx+mtree_left_ofs], rdi

	mov	[rbx+rcx+mtree_flags_ofs], eax
	mov	dword [rbx+rdi+mtree_flags_ofs], 1

	; restore rdi, rdx, rcx now that our node has changed
	mov	rdx, rdi
	mov	rdi, rcx
	mov	rcx, [rbx+rcx+mtree_right_ofs]

	; fallthrough to check2
.recurse_common_check2:
	test	rdx, rdx
	jz	.recurse_common_check3
	cmp	dword [rbx+rdx+mtree_flags_ofs], 1
	jne	.recurse_common_check3
	mov	r8, [rbx+rdx+mtree_left_ofs]
	test	r8, r8
	jz	.recurse_common_check3
	cmp	dword [rbx+r8+mtree_flags_ofs], 1
	jne	.recurse_common_check3
	; confusing bit of gear here, ror(rdi), with rdx == rdi.left, rcx == rdi.right
	mov	eax, [rbx+rdi+mtree_flags_ofs]
	mov	r8, [rbx+rdx+mtree_right_ofs]	; left node's right
	mov	[rbx+rdi+mtree_left_ofs], r8
	mov	[rbx+rdx+mtree_right_ofs], rdi
	
	mov	[rbx+rdx+mtree_flags_ofs], eax
	mov	dword [rbx+rdi+mtree_flags_ofs], 1

	; restore rdi, rdx, rcx now that our node has changed
	mov	rcx, rdi
	mov	rdi, rdx
	mov	rdx, [rbx+rdx+mtree_left_ofs]

	; fallthrough to check3
.recurse_common_check3:
	test	rcx, rcx
	jz	.recurse_common_return
	test	rdx, rdx
	jz	.recurse_common_return
	cmp	dword [rbx+rcx+mtree_flags_ofs], 1
	jne	.recurse_common_return
	cmp	dword [rbx+rdx+mtree_flags_ofs], 1
	jne	.recurse_common_return
	; flip w/ valid left and right, both of which's flags are 1
	mov	eax, [rbx+rdi+mtree_flags_ofs]
	xor	r8d, r8d
	not	eax
	mov	[rbx+rdx+mtree_flags_ofs], r8d
	mov	[rbx+rcx+mtree_flags_ofs], r8d
	and	eax, 1
	mov	[rbx+rdi+mtree_flags_ofs], eax
.recurse_common_return:
	mov	rax, rdi
	ret
calign
.recurse_right:
	push	rdi
	mov	rdi, rcx	; right
	call	.recurse
	pop	rdi
	mov	rdx, [rbx+rdi+mtree_left_ofs]
	mov	rcx, [rbx+rdi+mtree_right_ofs]
	cmp	rcx, rax
	je	.recurse_common
	mov	[rbx+rdi+mtree_right_ofs], rax
	mov	rcx, rax
	jmp	.recurse_common
calign
.recurse_r12ret:
	mov	rax, r12
	ret

	
end if


if used mtree$delete | defined include_everything
	; two arguments: rdi == mtree object, rsi == key to remove
	; if multi is set, this will remove ALL key/vals that match
	; returns count of # deleted in rax
falign
mtree$delete:
	prolog	mtree$delete
	push	rbx r12 r13 r14 rbp
	xor	r13d, r13d
	mov	r14, [rdi+mtree_base_ofs]
	mov	rbp, [rdi+mtree_current_root_ofs]
	mov	rbx, rdi
	mov	r12, rsi
.outer_loop:
	cmp	qword [r14+rbp+mtree_root_ofs], 0
	je	.return
	; find the node
	mov	rdi, [r14+rbp+mtree_root_ofs]
calign
.locate_node:
	mov	r8d, [r14+rdi+mtree_items_ofs]
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	sub	r8d, 1
	lea	rsi, [r14+rdi+mtree_firstkey_ofs]
	mov	r10d, r8d			; hi/items-1
	test	rdx, rdx
	jz	.locate_node_checkright
	cmp	r12, [rsi]
	jb	.locate_node_goleft		; left if key < rdi.keys[0]
	je	.node_found			; if key == rdi.keys[0], this node will do
.locate_node_checkright:
	test	rcx, rcx
	jz	.node_found
	shl	r8d, 4
	cmp	r12, [rsi+r8]			; right if key > rdi.keys[rdi.itemcount-1]
	jbe	.node_found
	; otherwise, travel right
	mov	rdi, rcx
	jmp	.locate_node
calign
.locate_node_goleft:
	mov	rdi, rdx
	jmp	.locate_node
calign
.node_found:
	; find the actual spot
	xor	r8d, r8d			; lo
	test	r10d, r10d			; hi
	jz	.node_found_highcheck
calign
.node_found_search:
	lea	edx, [r8d+r10d]
	shr	edx, 1				; mid = (lo+hi) >> 1
	mov	eax, edx
	lea	ecx, [edx+1]			; mid+1
	lea	r11d, [edx-1]			; mid-1
	shl	eax, 4
	cmp	r12, [rsi+rax]
	cmovbe	r10d, r11d
	cmova	r8d, ecx
	cmp	r8d, r10d
	jl	.node_found_search
.node_found_highcheck:
	mov	eax, r10d
	lea	ecx, [r10d+1]
	xor	r8d, r8d
	cmp	r10d, 0
	cmovl	eax, ecx
	jl	.node_found_proceed
	shl	eax, 4
	cmp	[rsi+rax], r12
	cmovb	eax, ecx
	cmovae	eax, r10d
.node_found_proceed:
	; if eax == item count, we didn't find the key
	mov	r8d, eax
	mov	edx, [r14+rdi+mtree_items_ofs]
	shl	r8d, 4
	cmp	eax, edx
	je	.return
	; if the key at eax != r12, no deal
	cmp	[rsi+r8], r12
	jne	.return

	; otherwise, we have two scenarios here
	; if items == 1, we need to do tree modifications
	; otherwise, a simple removal of our 16 bytes and we are good
	cmp	edx, 1
	je	.treemodify
	sub	edx, 1
	cmp	eax, edx		; if our spot was items-1, simple, no move required
	je	.simple_atend
	; otherwise, edx-eax-1 shl 4 is the # of bytes we need to move
	sub	edx, eax
	shl	eax, 4
	shl	edx, 4
	sub	dword [r14+rdi+mtree_items_ofs], 1
	; if we are counting, decrease the item count
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jz	.simple_skipcounting
	sub	qword [r14+rbp+mtree_count_ofs], 1
.simple_skipcounting:
	lea	rdi, [rsi+rax]
	lea	rsi, [rsi+rax+16]
	call	memmove
	; if multi is not set, we are all done
	add	r13, 1
	test	dword [rbx+mtree_settings_ofs], mtree_multi
	jnz	.outer_loop		; start from ground zero because we have no idea whether this key spans multiple nodes
	; otherwise, fallthrough to return
.return:
	mov	rax, r13
	pop	rbp r14 r13 r12 rbx
	epilog
calign
.simple_atend:
	sub	dword [r14+rdi+mtree_items_ofs], 1
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jz	.simple_atend_skipcounting
	sub	qword [r14+rbp+mtree_count_ofs], 1
.simple_atend_skipcounting:
	add	r13, 1
	test	dword [rbx+mtree_settings_ofs], mtree_multi
	jnz	.outer_loop		; start from ground zero because we have no idea whether this key spans multiple nodes
	mov	rax, r13
	pop	rbp r14 r13 r12 rbx
	epilog
calign
.treemodify:
	; so things are considerably more complicated here.
	; rdi == the node we are removing from our tree structure, which ultimately we need to save as we recursively descend
	; the tree from the root node again... thankfully this only ever happens when the node item count reaches 1 (effectively zero)
	push	r15
	mov	r15, rdi		; our comparison operator
	mov	rdi, [r14+rbp+mtree_root_ofs]
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	eax, 1
	xor	r8d, r8d
	test	rdx, rdx
	jz	.treemodify_skiprootleft
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	cmove	eax, r8d
.treemodify_skiprootleft:
	test	rcx, rcx
	jz	.treemodify_skiprootright
	cmp	dword [r14+rcx+mtree_flags_ofs], 1
	cmove	eax, r8d
.treemodify_skiprootright:
	test	eax, eax
	jz	.treemodify_norootchange
	mov	dword [r14+rdi+mtree_flags_ofs], 1
.treemodify_norootchange:
	; rdi is already set for recursive descent
	call	.treemodify_recurse
	pop	r15
	mov	rdi, [r14+rbp+mtree_root_ofs]
	cmp	rax, rdi
	jne	.treemodify_newroot
.treemodify_newroot_set:
	test	rdi, rdi
	jz	.treemodify_noreset
	mov	dword [r14+rdi+mtree_flags_ofs], 0
.treemodify_noreset:
	add	r13, 1
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jz	.treemodify_skipcounting
	sub	qword [r14+rbp+mtree_count_ofs], 1
.treemodify_skipcounting:
	test	dword [rbx+mtree_settings_ofs], mtree_multi
	jnz	.outer_loop		; start from ground zero because we have no idea whether this key spans multiple nodes
	mov	rax, r13
	pop	rbp r14 r13 r12 rbx
	epilog
.treemodify_newroot:
	mov	[r14+rbp+mtree_root_ofs], rax
	mov	rdi, rax
	jmp	.treemodify_newroot_set
falign
.ror:
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	eax, [r14+rdi+mtree_flags_ofs]
	mov	r8, [r14+rdx+mtree_right_ofs]
	mov	[r14+rdi+mtree_left_ofs], r8
	mov	[r14+rdx+mtree_right_ofs], rdi
	mov	[r14+rdx+mtree_flags_ofs], eax
	mov	dword [r14+rdi+mtree_flags_ofs], 1
	mov	rax, rdx
	ret
falign
.rol:
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	eax, [r14+rdi+mtree_flags_ofs]
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_right_ofs], r8
	mov	[r14+rcx+mtree_left_ofs], rdi
	mov	[r14+rcx+mtree_flags_ofs], eax
	mov	dword [r14+rdi+mtree_flags_ofs], 1
	mov	rax, rcx
	ret
falign
.treemodify_recurse:
	; rdi == rooted node, r15 is the node that key r12 lives in
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	cmp	r12, [r14+rdi+mtree_firstkey_ofs]
	jae	.treemodify_recurse_notleft
	mov	eax, 1
	xor	r8d, r8d
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	cmove	eax, r8d
	je	.treemodify_recurse_nomoveleft
	mov	r9, [r14+rdx+mtree_left_ofs]
	test	r9, r9
	jz	.treemodify_recurse_moveleft
	cmp	dword [r14+r9+mtree_flags_ofs], 1
	je	.treemodify_recurse_nomoveleft
.treemodify_recurse_moveleft:
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	test	r8, r8
	jz	.treemodify_recurse_nomoveleft
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.treemodify_recurse_nomoveleft
	push	rdi
	mov	rdi, rdx
	call	.ror
	pop	rdi
	mov	[r14+rdi+mtree_right_ofs], rax
	call	.rol
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	; fallthrough to nomoveleft:
.treemodify_recurse_nomoveleft:
	; h.left = delete(h.left, key);
	push	rdi
	mov	rdi, rdx
	call	.treemodify_recurse
	pop	rdi
	mov	[r14+rdi+mtree_left_ofs], rax

.treemodify_recurse_bal:
	; rdi is only one known good
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	rdx, [r14+rdi+mtree_left_ofs]
	test	rcx, rcx	
	jz	.treemodify_recurse_bal_norol
	cmp	dword [r14+rcx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_norol
	call	.rol
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
.treemodify_recurse_bal_norol:
	test	rdx, rdx
	jz	.treemodify_recurse_bal_noror
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_noror
	mov	r8, [r14+rdx+mtree_left_ofs]
	test	r8, r8
	jz	.treemodify_recurse_bal_noror
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_noror
	call	.ror
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
.treemodify_recurse_bal_noror:
	test	rdx, rdx
	jz	.treemodify_recurse_bal_return
	test	rcx, rcx
	jz	.treemodify_recurse_bal_return
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_return
	cmp	dword [r14+rcx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_return
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
.treemodify_recurse_bal_return:
	mov	rax, rdi
	ret
calign
.treemodify_recurse_notleft:
	; rdx == rdi.left, rcx == rdi.right
	test	rdx, rdx
	jz	.treemodify_recurse_notleft_noror
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_notleft_noror
	call	.ror
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
.treemodify_recurse_notleft_noror:
	cmp	r15, rdi
	jne	.treemodify_recurse_notnullret
	test	rcx, rcx
	jnz	.treemodify_recurse_notnullret

	; add the node at r15/rdi to the tree's freelist
	mov	rcx, [r14+mtree_free_ofs]
	mov	[r14+rdi], rcx
	mov	[r14+mtree_free_ofs], rdi

	; unlink it from the sort order linked list
	; rdi is already set:
	call	.linkedlist_delete

	xor	eax, eax
	ret
calign
.treemodify_recurse_notnullret:
	cmp	dword [r14+rcx+mtree_flags_ofs], 0
	jne	.treemodify_recurse_nomoveright
	mov	r8, [r14+rcx+mtree_left_ofs]
	test	r8, r8
	jz	.treemodify_recurse_moveright
	cmp	dword [r14+r8+mtree_flags_ofs], 0
	jne	.treemodify_recurse_nomoveright
.treemodify_recurse_moveright:
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	
	mov	r8, [r14+rdx+mtree_left_ofs]
	test	r8, r8
	jz	.treemodify_recurse_nomoveright
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.treemodify_recurse_nomoveright
	call	.ror
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
.treemodify_recurse_nomoveright:
	cmp	r15, rdi
	jne	.treemodify_recurse_descend_right
	; rcx (rdi.right) is nonzero, preserve rdi
calign
.treemodify_recurse_minright:
	cmp	qword [r14+rcx+mtree_left_ofs], 0
	je	.treemodify_recurse_minright_found
	mov	rcx, [r14+rcx+mtree_left_ofs]
	jmp	.treemodify_recurse_minright
calign
.treemodify_recurse_minright_found:
	; rcx is the min of the right of the node we are deleting.
	; set the node we are deleting's items + keyvals to rcx's
	; then remove rcx's node from rdi.right
	mov	edx, [r14+rcx+mtree_items_ofs]
	mov	[r14+rdi+mtree_items_ofs], edx
	push	rcx rdi
	lea	rsi, [r14+rcx+mtree_firstkey_ofs]
	lea	rdi, [r14+rdi+mtree_firstkey_ofs]
	shl	edx, 4
	call	memcpy
	mov	rdi, [rsp]
	mov	rdi, [r14+rdi+mtree_right_ofs]
	call	.dmin
	pop	rdi rcx

	; add the node at rcx to the tree's freelist
	mov	rdx, [r14+mtree_free_ofs]
	mov	[r14+rcx], rdx
	mov	[r14+mtree_free_ofs], rcx

	mov	[r14+rdi+mtree_right_ofs], rax

	push	rdi
	mov	rdi, rcx
	call	.linkedlist_delete
	pop	rdi

	jmp	.treemodify_recurse_bal
calign
.treemodify_recurse_descend_right:
	push	rdi
	mov	rdi, rcx
	call	.treemodify_recurse
	pop	rdi
	mov	[r14+rdi+mtree_right_ofs], rax
	jmp	.treemodify_recurse_bal

falign
.linkedlist_delete:
	; rdi == node we are removing from the first/last/next/prev sort order linkedlist
	; r14 is still our base, we are free to blast rdx/rcx/rax
	xor	r8d, r8d
	mov	rdx, [r14+rdi+mtree_next_ofs]
	mov	rcx, [r14+rdi+mtree_prev_ofs]
	cmp	rdi, [r14+rbp+mtree_first_ofs]
	je	.linkedlist_delete_isfirst
	cmp	rdi, [r14+rbp+mtree_last_ofs]
	je	.linkedlist_last_notfirst
	; not first and not last
	mov	[r14+rdx+mtree_prev_ofs], rcx
	mov	[r14+rcx+mtree_next_ofs], rdx
	ret
calign
.linkedlist_delete_isfirst:
	cmp	rdi, [r14+rbp+mtree_last_ofs]
	je	.linkedlist_first_and_last
	; otherwise, first, not last
	mov	[r14+rbp+mtree_first_ofs], rdx
	mov	[r14+rdx+mtree_prev_ofs], r8
	ret
calign
.linkedlist_first_and_last:
	mov	[r14+rbp+mtree_first_ofs], r8
	mov	[r14+rbp+mtree_last_ofs], r8
	ret

calign
.linkedlist_last_notfirst:
	mov	[r14+rbp+mtree_last_ofs], rcx
	mov	[r14+rcx+mtree_next_ofs], r8
	ret

falign
.dmin:
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	cmp	qword [r14+rdi+mtree_left_ofs], 0
	je	.dmin_nullret
	mov	eax, 1
	xor	r8d, r8d
	cmp	dword [r14+rdx+mtree_flags_ofs], 0
	jne	.dmin_nomoveleft
	mov	r9, [r14+rdx+mtree_left_ofs]
	test	r9, r9
	jz	.dmin_moveleft
	cmp	dword [r14+r9+mtree_flags_ofs], 0
	jne	.dmin_nomoveleft
.dmin_moveleft:
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	test	r8, r8
	jz	.dmin_nomoveleft
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.dmin_nomoveleft
	push	rdi
	mov	rdi, rdx
	call	.ror
	pop	rdi
	mov	[r14+rdi+mtree_right_ofs], rax
	call	.rol
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	; fallthrough to nomoveleft:
.dmin_nomoveleft:
	push	rdi
	mov	rdi, [r14+rdi+mtree_left_ofs]
	call	.dmin
	pop	rdi
	mov	[r14+rdi+mtree_left_ofs], rax
	; return balance(rdi)
	jmp	.treemodify_recurse_bal
calign
.dmin_nullret:
	xor	eax, eax
	ret

end if



if used mtree$find_value | defined include_everything
	; two arguments: rdi == mtree object, rsi == key
	; returns bool in eax as to whether we found it or not, and value in rdx if so
falign
mtree$find_value:
	prolog	mtree$find_value
	push	r12
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	r9, [rdi+mtree_base_ofs]
	mov	r12, rsi
	mov	rdi, [r9+rax+mtree_root_ofs]
	test	rdi, rdi
	jz	.return_false
calign
.locate_node:
	mov	r8d, [r9+rdi+mtree_items_ofs]
	mov	rdx, [r9+rdi+mtree_left_ofs]
	mov	rcx, [r9+rdi+mtree_right_ofs]
	sub	r8d, 1
	lea	rsi, [r9+rdi+mtree_firstkey_ofs]
	mov	r10d, r8d		; hi/items-1
	test	rdx, rdx
	jz	.locate_node_checkright
	cmp	r12, [rsi]
	cmovb	rdi, rdx
	jb	.locate_node
	je	.node_found
.locate_node_checkright:
	test	rcx, rcx
	jz	.node_found
	shl	r8d, 4
	cmp	r12, [rsi+r8]
	jbe	.node_found
	mov	rdi, rcx
	jmp	.locate_node
calign
.node_found:
	; find inner lower
	xor	r8d, r8d		; lo
	test	r10d, r10d		; hi
	jz	.node_found_highcheck
calign
.node_found_search:
	lea	edx, [r8d+r10d]
	shr	edx, 1			; mid = (lo+hi)>>1
	mov	eax, edx
	lea	ecx, [edx+1]		; mid+1
	lea	r11d, [edx-1]		; mid-1
	shl	eax, 4
	cmp	r12, [rsi+rax]
	cmovbe	r10d, r11d
	cmova	r8d, ecx
	cmp	r8d, r10d
	jl	.node_found_search
.node_found_highcheck:
	mov	eax, r10d
	lea	ecx, [r10d+1]
	cmp	r10d, 0
	cmovl	eax, ecx
	jl	.node_found_proceed
	shl	eax, 4
	cmp	[rsi+rax], r12
	cmovb	eax, ecx
	cmovae	eax, r10d
.node_found_proceed:
	; if eax == item count, past the end, return false
	mov	r8d, eax
	mov	edx, [r9+rdi+mtree_items_ofs]
	shl	r8d, 4
	cmp	eax, edx
	je	.return_false
	; if the key at eax != r12, no deal
	cmp	[rsi+r8], r12
	jne	.return_false
	mov	rdx, [rsi+r8+8]
	mov	eax, 1
	pop	r12
	epilog
calign
.return_false:
	xor	eax, eax
	pop	r12
	epilog

end if


if used mtree$min | defined include_everything
	; single argument in rdi: mtree object
	; returns bool in eax (false == empty tree), smallest key in rdx if true
falign
mtree$min:
	prolog	mtree$min
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	rdx, [rdi+mtree_base_ofs]
	mov	rcx, [rdx+rax+mtree_root_ofs]
	test	rcx, rcx
	jz	.return_false
calign
.locate_node:
	cmp	qword [rdx+rcx+mtree_left_ofs], 0
	je	.node_found
	mov	rcx, [rdx+rcx+mtree_left_ofs]
	jmp	.locate_node
calign
.node_found:
	mov	eax, 1
	mov	rdx, [rdx+rcx+mtree_firstkey_ofs]
	epilog
calign
.return_false:
	xor	eax, eax
	epilog

end if


if used mtree$max | defined include_everything
	; single argument in rdi: mtree object
	; returns bool in eax (false == empty tree), largest key in rdx if true
falign
mtree$max:
	prolog	mtree$max
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	rdx, [rdi+mtree_base_ofs]
	mov	rcx, [rdx+rax+mtree_root_ofs]
	test	rcx, rcx
	jz	.return_false
calign
.locate_node:
	cmp	qword [rdx+rcx+mtree_right_ofs], 0
	je	.node_found
	mov	rcx, [rdx+rcx+mtree_right_ofs]
	jmp	.locate_node
calign
.node_found:
	mov	r8d, [rdx+rcx+mtree_items_ofs]
	mov	eax, 1
	sub	r8d, 1
	shl	r8d, 4
	add	r8, rcx
	mov	rdx, [rdx+r8+mtree_firstkey_ofs]
	epilog
calign
.return_false:
	xor	eax, eax
	epilog

end if




	;
	;
	;
	;     ITERATOR goodies...
	;
	; an iterator in this case is specific to the mtree object itself, and as such is
	; named mtree_iter... since most often these will be stack allocated, instead of
	; creating a heap based object and constantly destroying it, we pass the iterator
	; pointer to the functions that assign/modify them and call it good
	;
	;
	;

if used mtree$find | used mtree$minimum | used mtree$maximum | used mtree$begin | used mtree$end | used mtree$lower_bound | used mtree$upper_bound | defined include_everything

mtree_iter_tree_ofs = 0		; copy of the mtree object pointer itself
mtree_iter_node_ofs = 8		; offset to the node this iterator points to, or 0 if invalid.
mtree_iter_pos_ofs = 16		; offset to the item position this iterator points to

mtree_iter_size = 24

end if

if used mtree_iter$valid | defined include_everything
	; single argument in rdi: mtree_iter object
	; NOTE: placeholder only really, you should just check for node !=0 yourself
	; returns bool in eax
falign
mtree_iter$valid:
	prolog	mtree_iter$valid
	mov	edx, 1
	xor	eax, eax
	cmp	qword [rdi+mtree_iter_node_ofs], 0
	cmovne	eax, edx
	epilog

end if


if used mtree_iter$compare | defined include_everything
	; two arguments: rdi == mtree_iter object, rsi == mtree_iter object
	; returns bool in eax as to whether they are same-same or not
falign
mtree_iter$compare:
	prolog	mtree_iter$compare
	xor	edx, edx
	mov	eax, 1
	mov	r8, [rsi]
	mov	r9, [rsi+8]
	mov	r10, [rsi+16]
	cmp	r8, [rdi]
	cmovne	eax, edx
	cmp	r9, [rdi+8]
	cmovne	eax, edx
	cmp	r10, [rdi+16]
	cmovne	eax, edx
	epilog

end if


if used mtree_iter$key_value | defined include_everything
	; single argument in rdi: mtree_iter object
	; assumes iterator is valid, and places key in rax, value in rdx
falign
mtree_iter$key_value:
	prolog	mtree_iter$key_value
	mov	r8d, [rdi+mtree_iter_pos_ofs]
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	shl	r8d, 4
	add	r8, [rdi+mtree_iter_node_ofs]
	mov	rdx, [rsi+mtree_base_ofs]
	mov	rax, [rdx+r8+mtree_firstkey_ofs]
	mov	rdx, [rdx+r8+mtree_firstkey_ofs+8]
	epilog

end if


if used mtree_iter$key | defined include_everything
	; single argument in rdi: mtree_iter object
	; assumes iterator is valid, and places key in rax
falign
mtree_iter$key:
	prolog	mtree_iter$key
	mov	r8d, [rdi+mtree_iter_pos_ofs]
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	shl	r8d, 4
	add	r8, [rdi+mtree_iter_node_ofs]
	mov	rdx, [rsi+mtree_base_ofs]
	mov	rax, [rdx+r8+mtree_firstkey_ofs]
	epilog

end if


if used mtree_iter$value | defined include_everything
	; single argument in rdi: mtree_iter object
	; assumes iterator is valid, and places value in rax
falign
mtree_iter$value:
	prolog	mtree_iter$value
	mov	r8d, [rdi+mtree_iter_pos_ofs]
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	shl	r8d, 4
	add	r8, [rdi+mtree_iter_node_ofs]
	mov	rdx, [rsi+mtree_base_ofs]
	mov	rax, [rdx+r8+mtree_firstkey_ofs+8]
	epilog

end if


if used mtree_iter$set_key_value | defined include_everything
	; three arguments: rdi == mtree_iter object, rsi == new key, rdx == new value
	; (DOES NOT DO ANY VALIDITY CHECKING/tree reorg)
falign
mtree_iter$set_key_value:
	prolog	mtree_iter$set_key_value
	mov	r8d, [rdi+mtree_iter_pos_ofs]
	mov	r10, rsi
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	shl	r8d, 4
	mov	r11, rdx
	add	r8, [rdi+mtree_iter_node_ofs]
	mov	rdx, [rsi+mtree_base_ofs]
	mov	[rdx+r8+mtree_firstkey_ofs], r10
	mov	[rdx+r8+mtree_firstkey_ofs+8], r11
	epilog

end if


if used mtree_iter$set_key | defined include_everything
	; two arguments: rdi == mtree_iter object, rsi == new key
	; (DOES NOT DO ANY VALIDITY CHECKING/tree reorg)
falign
mtree_iter$set_key:
	prolog	mtree_iter$set_key
	mov	r8d, [rdi+mtree_iter_pos_ofs]
	mov	r10, rsi
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	shl	r8d, 4
	add	r8, [rdi+mtree_iter_node_ofs]
	mov	rdx, [rsi+mtree_base_ofs]
	mov	[rdx+r8+mtree_firstkey_ofs], r10
	epilog

end if


if used mtree_iter$set_value | defined include_everything
	; two arguments: rdi == mtree_iter object, rsi == new value
falign
mtree_iter$set_value:
	prolog	mtree_iter$set_value
	mov	r8d, [rdi+mtree_iter_pos_ofs]
	mov	r10, rsi
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	shl	r8d, 4
	add	r8, [rdi+mtree_iter_node_ofs]
	mov	rdx, [rsi+mtree_base_ofs]
	mov	[rdx+r8+mtree_firstkey_ofs+8], r10
	epilog

end if


if used mtree_iter$prev | defined include_everything
	; single argument in rdi: mtree_iter object
	; moves back one spot or sets to invalid if we walk past the first one
	; returns bool in eax as to whether we are good or not (false == invalid iter)
falign
mtree_iter$prev:
	prolog	mtree_iter$prev
	cmp	qword [rdi+mtree_iter_node_ofs], 0
	je	.invalid
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	cmp	dword [rdi+mtree_iter_pos_ofs], 0
	je	.prevnode
	sub	dword [rdi+mtree_iter_pos_ofs], 1
	mov	eax, 1
	epilog
calign
.prevnode:
	mov	rdx, [rsi+mtree_base_ofs]
	mov	rcx, [rdi+mtree_iter_node_ofs]
	mov	r8, [rdx+rcx+mtree_prev_ofs]
	test	r8, r8
	jz	.invalid
	mov	r9d, [rdx+r8+mtree_items_ofs]
	mov	[rdi+mtree_iter_node_ofs], r8
	sub	r9d, 1
	mov	[rdi+mtree_iter_pos_ofs], r9d
	mov	eax, 1
	epilog
calign
.invalid:
	mov	qword [rdi+mtree_iter_node_ofs], 0
	xor	eax, eax
	epilog

end if

if used mtree_iter$next | defined include_everything
	; single argument in rdi: mtree_iter object
	; moves forward one spot or sets to invalid if we walk past the end
	; returns bool in eax as to whether we are good or not (false == invalid iter)
falign
mtree_iter$next:
	prolog	mtree_iter$next
	cmp	qword [rdi+mtree_iter_node_ofs], 0
	je	.invalid
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	add	dword [rdi+mtree_iter_pos_ofs], 1
	mov	rdx, [rsi+mtree_base_ofs]
	mov	rcx, [rdi+mtree_iter_node_ofs]
	mov	r8d, [rdx+rcx+mtree_items_ofs]
	cmp	r8d, [rdi+mtree_iter_pos_ofs]
	je	.nextnode
	mov	eax, 1
	epilog
calign
.nextnode:
	mov	r8, [rdx+rcx+mtree_next_ofs]
	test	r8, r8
	jz	.invalid
	mov	[rdi+mtree_iter_node_ofs], r8
	mov	dword [rdi+mtree_iter_pos_ofs], 0
	mov	eax, 1
	epilog
calign
.invalid:
	mov	qword [rdi+mtree_iter_node_ofs], 0
	xor	eax, eax
	epilog

end if


if used mtree_iter$delete | defined include_everything
	; single argument in rdi: a valid mtree_iter object
	; NOTE: this allows specific item deletion from a multi-tree
	; and fully invalidates the iterator (and conceivably every one after it)
falign
mtree_iter$delete:
	prolog	mtree_iter$delete
	push	rdi
	call	mtree_iter$key
	pop	rdi
	mov	rsi, [rdi+mtree_iter_tree_ofs]
	push	rbx r12 r13 r14 rbp
	xor	r13d, r13d
	mov	r14, [rsi+mtree_base_ofs]
	mov	rbp, [rsi+mtree_current_root_ofs]
	mov	rbx, rsi
	mov	r12, rax
	mov	eax, [rdi+mtree_iter_pos_ofs]	; position of our key
	mov	rdi, [rdi+mtree_iter_node_ofs]	; offset of our node

	; copy-paste from normal delete's .node_found_proceed right the way through here:
.node_found_proceed:
	; if eax == item count, we didn't find the key
	mov	r8d, eax
	mov	edx, [r14+rdi+mtree_items_ofs]
	lea	rsi, [r14+rdi+mtree_firstkey_ofs]
	shl	r8d, 4
	cmp	eax, edx
	je	.return
	; if the key at eax != r12, no deal
	cmp	[rsi+r8], r12
	jne	.return

	; otherwise, we have two scenarios here
	; if items == 1, we need to do tree modifications
	; otherwise, a simple removal of our 16 bytes and we are good
	cmp	edx, 1
	je	.treemodify
	sub	edx, 1
	cmp	eax, edx		; if our spot was items-1, simple, no move required
	je	.simple_atend
	; otherwise, edx-eax-1 shl 4 is the # of bytes we need to move
	sub	edx, eax
	shl	eax, 4
	shl	edx, 4
	sub	dword [r14+rdi+mtree_items_ofs], 1
	; if we are counting, decrease the item count
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jz	.simple_skipcounting
	sub	qword [r14+rbp+mtree_count_ofs], 1
.simple_skipcounting:
	lea	rdi, [rsi+rax]
	lea	rsi, [rsi+rax+16]
	call	memmove
	; if multi is not set, we are all done
	add	r13, 1
	; otherwise, fallthrough to return
.return:
	mov	rax, r13
	pop	rbp r14 r13 r12 rbx
	epilog
calign
.simple_atend:
	sub	dword [r14+rdi+mtree_items_ofs], 1
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jz	.simple_atend_skipcounting
	sub	qword [r14+rbp+mtree_count_ofs], 1
.simple_atend_skipcounting:
	add	r13, 1
	mov	rax, r13
	pop	rbp r14 r13 r12 rbx
	epilog
calign
.treemodify:
	; so things are considerably more complicated here.
	; rdi == the node we are removing from our tree structure, which ultimately we need to save as we recursively descend
	; the tree from the root node again... thankfully this only ever happens when the node item count reaches 1 (effectively zero)
	push	r15
	mov	r15, rdi		; our comparison operator
	mov	rdi, [r14+rbp+mtree_root_ofs]
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	eax, 1
	xor	r8d, r8d
	test	rdx, rdx
	jz	.treemodify_skiprootleft
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	cmove	eax, r8d
.treemodify_skiprootleft:
	test	rcx, rcx
	jz	.treemodify_skiprootright
	cmp	dword [r14+rcx+mtree_flags_ofs], 1
	cmove	eax, r8d
.treemodify_skiprootright:
	test	eax, eax
	jz	.treemodify_norootchange
	mov	dword [r14+rdi+mtree_flags_ofs], 1
.treemodify_norootchange:
	; rdi is already set for recursive descent
	call	.treemodify_recurse
	pop	r15
	mov	rdi, [r14+rbp+mtree_root_ofs]
	cmp	rax, rdi
	jne	.treemodify_newroot
.treemodify_newroot_set:
	test	rdi, rdi
	jz	.treemodify_noreset
	mov	dword [r14+rdi+mtree_flags_ofs], 0
.treemodify_noreset:
	add	r13, 1
	test	dword [rbx+mtree_settings_ofs], mtree_counting
	jz	.treemodify_skipcounting
	sub	qword [r14+rbp+mtree_count_ofs], 1
.treemodify_skipcounting:
	mov	rax, r13
	pop	rbp r14 r13 r12 rbx
	epilog
.treemodify_newroot:
	mov	[r14+rbp+mtree_root_ofs], rax
	mov	rdi, rax
	jmp	.treemodify_newroot_set
falign
.ror:
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	eax, [r14+rdi+mtree_flags_ofs]
	mov	r8, [r14+rdx+mtree_right_ofs]
	mov	[r14+rdi+mtree_left_ofs], r8
	mov	[r14+rdx+mtree_right_ofs], rdi
	mov	[r14+rdx+mtree_flags_ofs], eax
	mov	dword [r14+rdi+mtree_flags_ofs], 1
	mov	rax, rdx
	ret
falign
.rol:
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	eax, [r14+rdi+mtree_flags_ofs]
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_right_ofs], r8
	mov	[r14+rcx+mtree_left_ofs], rdi
	mov	[r14+rcx+mtree_flags_ofs], eax
	mov	dword [r14+rdi+mtree_flags_ofs], 1
	mov	rax, rcx
	ret
falign
.treemodify_recurse:
	; rdi == rooted node, r15 is the node that key r12 lives in
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	cmp	r12, [r14+rdi+mtree_firstkey_ofs]
	jae	.treemodify_recurse_notleft
	mov	eax, 1
	xor	r8d, r8d
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	cmove	eax, r8d
	je	.treemodify_recurse_nomoveleft
	mov	r9, [r14+rdx+mtree_left_ofs]
	test	r9, r9
	jz	.treemodify_recurse_moveleft
	cmp	dword [r14+r9+mtree_flags_ofs], 1
	je	.treemodify_recurse_nomoveleft
.treemodify_recurse_moveleft:
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	test	r8, r8
	jz	.treemodify_recurse_nomoveleft
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.treemodify_recurse_nomoveleft
	push	rdi
	mov	rdi, rdx
	call	.ror
	pop	rdi
	mov	[r14+rdi+mtree_right_ofs], rax
	call	.rol
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	; fallthrough to nomoveleft:
.treemodify_recurse_nomoveleft:
	; h.left = delete(h.left, key);
	push	rdi
	mov	rdi, rdx
	call	.treemodify_recurse
	pop	rdi
	mov	[r14+rdi+mtree_left_ofs], rax

.treemodify_recurse_bal:
	; rdi is only one known good
	mov	rcx, [r14+rdi+mtree_right_ofs]
	mov	rdx, [r14+rdi+mtree_left_ofs]
	test	rcx, rcx	
	jz	.treemodify_recurse_bal_norol
	cmp	dword [r14+rcx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_norol
	call	.rol
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
.treemodify_recurse_bal_norol:
	test	rdx, rdx
	jz	.treemodify_recurse_bal_noror
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_noror
	mov	r8, [r14+rdx+mtree_left_ofs]
	test	r8, r8
	jz	.treemodify_recurse_bal_noror
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_noror
	call	.ror
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
.treemodify_recurse_bal_noror:
	test	rdx, rdx
	jz	.treemodify_recurse_bal_return
	test	rcx, rcx
	jz	.treemodify_recurse_bal_return
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_return
	cmp	dword [r14+rcx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_bal_return
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
.treemodify_recurse_bal_return:
	mov	rax, rdi
	ret
calign
.treemodify_recurse_notleft:
	; rdx == rdi.left, rcx == rdi.right
	test	rdx, rdx
	jz	.treemodify_recurse_notleft_noror
	cmp	dword [r14+rdx+mtree_flags_ofs], 1
	jne	.treemodify_recurse_notleft_noror
	call	.ror
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
.treemodify_recurse_notleft_noror:
	cmp	r15, rdi
	jne	.treemodify_recurse_notnullret
	test	rcx, rcx
	jnz	.treemodify_recurse_notnullret

	; add the node at r15/rdi to the tree's freelist
	mov	rcx, [r14+mtree_free_ofs]
	mov	[r14+rdi], rcx
	mov	[r14+mtree_free_ofs], rdi

	; unlink it from the sort order linked list
	; rdi is already set:
	call	.linkedlist_delete

	xor	eax, eax
	ret
calign
.treemodify_recurse_notnullret:
	cmp	dword [r14+rcx+mtree_flags_ofs], 0
	jne	.treemodify_recurse_nomoveright
	mov	r8, [r14+rcx+mtree_left_ofs]
	test	r8, r8
	jz	.treemodify_recurse_moveright
	cmp	dword [r14+r8+mtree_flags_ofs], 0
	jne	.treemodify_recurse_nomoveright
.treemodify_recurse_moveright:
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	
	mov	r8, [r14+rdx+mtree_left_ofs]
	test	r8, r8
	jz	.treemodify_recurse_nomoveright
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.treemodify_recurse_nomoveright
	call	.ror
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
.treemodify_recurse_nomoveright:
	cmp	r15, rdi
	jne	.treemodify_recurse_descend_right
	; rcx (rdi.right) is nonzero, preserve rdi
calign
.treemodify_recurse_minright:
	cmp	qword [r14+rcx+mtree_left_ofs], 0
	je	.treemodify_recurse_minright_found
	mov	rcx, [r14+rcx+mtree_left_ofs]
	jmp	.treemodify_recurse_minright
calign
.treemodify_recurse_minright_found:
	; rcx is the min of the right of the node we are deleting.
	; set the node we are deleting's items + keyvals to rcx's
	; then remove rcx's node from rdi.right
	mov	edx, [r14+rcx+mtree_items_ofs]
	mov	[r14+rdi+mtree_items_ofs], edx
	push	rcx rdi
	lea	rsi, [r14+rcx+mtree_firstkey_ofs]
	lea	rdi, [r14+rdi+mtree_firstkey_ofs]
	shl	edx, 4
	call	memcpy
	mov	rdi, [rsp]
	mov	rdi, [r14+rdi+mtree_right_ofs]
	call	.dmin
	pop	rdi rcx

	; add the node at rcx to the tree's freelist
	mov	rdx, [r14+mtree_free_ofs]
	mov	[r14+rcx], rdx
	mov	[r14+mtree_free_ofs], rcx

	mov	[r14+rdi+mtree_right_ofs], rax

	push	rdi
	mov	rdi, rcx
	call	.linkedlist_delete
	pop	rdi

	jmp	.treemodify_recurse_bal
calign
.treemodify_recurse_descend_right:
	push	rdi
	mov	rdi, rcx
	call	.treemodify_recurse
	pop	rdi
	mov	[r14+rdi+mtree_right_ofs], rax
	jmp	.treemodify_recurse_bal

falign
.linkedlist_delete:
	; rdi == node we are removing from the first/last/next/prev sort order linkedlist
	; r14 is still our base, we are free to blast rdx/rcx/rax
	xor	r8d, r8d
	mov	rdx, [r14+rdi+mtree_next_ofs]
	mov	rcx, [r14+rdi+mtree_prev_ofs]
	cmp	rdi, [r14+rbp+mtree_first_ofs]
	je	.linkedlist_delete_isfirst
	cmp	rdi, [r14+rbp+mtree_last_ofs]
	je	.linkedlist_last_notfirst
	; not first and not last
	mov	[r14+rdx+mtree_prev_ofs], rcx
	mov	[r14+rcx+mtree_next_ofs], rdx
	ret
calign
.linkedlist_delete_isfirst:
	cmp	rdi, [r14+rbp+mtree_last_ofs]
	je	.linkedlist_first_and_last
	; otherwise, first, not last
	mov	[r14+rbp+mtree_first_ofs], rdx
	mov	[r14+rdx+mtree_prev_ofs], r8
	ret
calign
.linkedlist_first_and_last:
	mov	[r14+rbp+mtree_first_ofs], r8
	mov	[r14+rbp+mtree_last_ofs], r8
	ret

calign
.linkedlist_last_notfirst:
	mov	[r14+rbp+mtree_last_ofs], rcx
	mov	[r14+rcx+mtree_next_ofs], r8
	ret

falign
.dmin:
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	cmp	qword [r14+rdi+mtree_left_ofs], 0
	je	.dmin_nullret
	mov	eax, 1
	xor	r8d, r8d
	cmp	dword [r14+rdx+mtree_flags_ofs], 0
	jne	.dmin_nomoveleft
	mov	r9, [r14+rdx+mtree_left_ofs]
	test	r9, r9
	jz	.dmin_moveleft
	cmp	dword [r14+r9+mtree_flags_ofs], 0
	jne	.dmin_nomoveleft
.dmin_moveleft:
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	test	r8, r8
	jz	.dmin_nomoveleft
	cmp	dword [r14+r8+mtree_flags_ofs], 1
	jne	.dmin_nomoveleft
	push	rdi
	mov	rdi, rdx
	call	.ror
	pop	rdi
	mov	[r14+rdi+mtree_right_ofs], rax
	call	.rol
	mov	rdi, rax
	mov	rdx, [r14+rdi+mtree_left_ofs]
	mov	rcx, [r14+rdi+mtree_right_ofs]
	; flip(rdi)
	mov	r9d, [r14+rdi+mtree_flags_ofs]
	mov	r10d, [r14+rdx+mtree_flags_ofs]
	mov	r11d, [r14+rcx+mtree_flags_ofs]
	not	r9d
	not	r10d
	not	r11d
	and	r9d, 1
	and	r10d, 1
	and	r11d, 1
	mov	r8, [r14+rcx+mtree_left_ofs]
	mov	[r14+rdi+mtree_flags_ofs], r9d
	mov	[r14+rdx+mtree_flags_ofs], r10d
	mov	[r14+rcx+mtree_flags_ofs], r11d
	; fallthrough to nomoveleft:
.dmin_nomoveleft:
	push	rdi
	mov	rdi, [r14+rdi+mtree_left_ofs]
	call	.dmin
	pop	rdi
	mov	[r14+rdi+mtree_left_ofs], rax
	; return balance(rdi)
	jmp	.treemodify_recurse_bal
calign
.dmin_nullret:
	xor	eax, eax
	ret

end if



if used mtree$find | defined include_everything
	; three arguments: rdi == mtree object, rsi == key, rdx == ptr to mtree_iter_size object we'll initialize
	; this returns 0 in eax if we didn't find it (and also sets iter's node to 0 whcih is also invalid)
	; or, if we populated a valid iter, returns 1 in eax
falign
mtree$find:
	prolog	mtree$find
	mov	[rdx+mtree_iter_tree_ofs], rdi
	mov	qword [rdx+mtree_iter_node_ofs], 0
	mov	rax, [rdi+mtree_current_root_ofs]
	push	r12 r13
	mov	r13, rdx
	mov	r9, [rdi+mtree_base_ofs]
	mov	r12, rsi
	mov	rdi, [r9+rax+mtree_root_ofs]
	test	rdi, rdi
	jz	.return_false
calign
.locate_node:
	mov	r8d, [r9+rdi+mtree_items_ofs]
	mov	rdx, [r9+rdi+mtree_left_ofs]
	mov	rcx, [r9+rdi+mtree_right_ofs]
	sub	r8d, 1
	lea	rsi, [r9+rdi+mtree_firstkey_ofs]
	mov	r10d, r8d		; hi/items-1
	test	rdx, rdx
	jz	.locate_node_checkright
	cmp	r12, [rsi]
	cmovb	rdi, rdx
	jb	.locate_node
	je	.node_found
.locate_node_checkright:
	test	rcx, rcx
	jz	.node_found
	shl	r8d, 4
	cmp	r12, [rsi+r8]
	jbe	.node_found
	mov	rdi, rcx
	jmp	.locate_node
calign
.node_found:
	; find inner lower
	xor	r8d, r8d		; lo
	test	r10d, r10d		; hi
	jz	.node_found_highcheck
calign
.node_found_search:
	lea	edx, [r8d+r10d]
	shr	edx, 1			; mid = (lo+hi)>>1
	mov	eax, edx
	lea	ecx, [edx+1]		; mid+1
	lea	r11d, [edx-1]		; mid-1
	shl	eax, 4
	cmp	r12, [rsi+rax]
	cmovbe	r10d, r11d
	cmova	r8d, ecx
	cmp	r8d, r10d
	jl	.node_found_search
.node_found_highcheck:
	mov	eax, r10d
	lea	ecx, [r10d+1]
	cmp	r10d, 0
	cmovl	eax, ecx
	jl	.node_found_proceed
	shl	eax, 4
	cmp	[rsi+rax], r12
	cmovb	eax, ecx
	cmovae	eax, r10d
.node_found_proceed:
	; if eax == item count, past the end, return false
	mov	r8d, eax
	mov	edx, [r9+rdi+mtree_items_ofs]
	shl	r8d, 4
	cmp	eax, edx
	je	.return_false
	; if the key at eax != r12, no deal
	cmp	[rsi+r8], r12
	jne	.return_false
	mov	[r13+mtree_iter_node_ofs], rdi
	mov	[r13+mtree_iter_pos_ofs], rax
	mov	eax, 1
	pop	r13 r12
	epilog
calign
.return_false:
	xor	eax, eax
	pop	r13 r12
	epilog

end if


if used mtree$begin | defined include_everything
	; two arguments: rdi == mtree object, rsi == ptr to mtree_iter_size object we'll initialize
	; if mtree is empty, the resultant iter will also be invalid (mtree_iter$valid would return false)
falign
mtree$begin:
	prolog	mtree$begin
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	rdx, [rdi+mtree_base_ofs]
	mov	[rsi+mtree_iter_tree_ofs], rdi
	xor	r8d, r8d
	mov	rcx, [rdx+rax+mtree_first_ofs]
	mov	[rsi+mtree_iter_pos_ofs], r8
	mov	[rsi+mtree_iter_node_ofs], rcx
	epilog

end if

if used mtree$end | defined include_everything
	; two arguments: rdi == mtree object, rsi == ptr to mtree_iter_size object we'll initialize
	; if mtree is empty, the resultant iter will also be invalid (mtree_iter$valid would return false)
	; if however it is not empty, this gets set to the ACTUAL end, such that mtree_iter$prev would result in a valid
	; data pointer. (end is pointing to "where the next key would go on the end" and not the last key)
falign
mtree$end:
	prolog	mtree$end
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	rdx, [rdi+mtree_base_ofs]
	mov	[rsi+mtree_iter_tree_ofs], rdi
	mov	rcx, [rdx+rax+mtree_last_ofs]
	test	rcx, rcx
	jz	.invalid
	mov	r8d, [rdx+rcx+mtree_items_ofs]
	mov	[rsi+mtree_iter_node_ofs], rcx
	mov	[rsi+mtree_iter_pos_ofs], r8
	epilog
calign
.invalid:
	mov	[rsi+mtree_iter_node_ofs], rcx
	mov	[rsi+mtree_iter_pos_ofs], r8
	epilog

end if
	

if used mtree$minimum | defined include_everything
	; two arguments: rdi == mtree object, rsi == ptr to mtree_iter_size object we'll initialize
	; if mtree is empty, the resultant iter will also be invalid (mtree_iter$valid would return false)
falign
mtree$minimum:
	prolog	mtree$minimum
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	rdx, [rdi+mtree_base_ofs]
	mov	[rsi+mtree_iter_tree_ofs], rdi
	xor	r8d, r8d
	mov	rcx, [rdx+rax+mtree_first_ofs]
	mov	[rsi+mtree_iter_pos_ofs], r8
	mov	[rsi+mtree_iter_node_ofs], rcx
	epilog

end if


if used mtree$maximum | defined include_everything
	; two arguments: rdi == mtree object, rsi == ptr to mtree_iter_size object we'll initialize
	; if mtree is empty, the resultant iter will also be invalid (mtree_iter$valid would return false)
	; note: this is the same as mtree$end, only we backup one such that the iterator is pointing to
	; the very last item
falign
mtree$maximum:
	prolog	mtree$maximum
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	rdx, [rdi+mtree_base_ofs]
	mov	[rsi+mtree_iter_tree_ofs], rdi
	mov	rcx, [rdx+rax+mtree_last_ofs]
	test	rcx, rcx
	jz	.invalid
	mov	r8d, [rdx+rcx+mtree_items_ofs]
	mov	[rsi+mtree_iter_node_ofs], rcx
	sub	r8d, 1
	mov	[rsi+mtree_iter_pos_ofs], r8
	epilog
calign
.invalid:
	mov	[rsi+mtree_iter_node_ofs], rcx
	mov	[rsi+mtree_iter_pos_ofs], r8
	epilog

end if





if used mtree$lower_bound | defined include_everything
	; three arguments: rdi == mtree object, rsi == key, rdx == ptr to mtree_iter_size object that we'll initialize
	; returns bool in eax as to whether we set the iterator to valid or not
	; (first key which is not less than argument, aka insert pos, or greater-than-or-equal-to argument)
falign
mtree$lower_bound:
	prolog	mtree$lower_bound
	push	r12 r13
	mov	rax, [rdi+mtree_current_root_ofs]
	mov	[rdx+mtree_iter_tree_ofs], rdi
	mov	r13, rdx
	xor	r8d, r8d
	mov	r9, [rdi+mtree_base_ofs]
	mov	r12, rsi
	mov	[rdx+mtree_iter_node_ofs], r8
	mov	[rdx+mtree_iter_pos_ofs], r8
	mov	rdi, [r9+rax+mtree_root_ofs]
	test	rdi, rdi
	jz	.return_false
calign
.locate_node:
	mov	r8d, [r9+rdi+mtree_items_ofs]
	mov	rdx, [r9+rdi+mtree_left_ofs]
	mov	rcx, [r9+rdi+mtree_right_ofs]
	sub	r8d, 1
	lea	rsi, [r9+rdi+mtree_firstkey_ofs]
	mov	r10d, r8d		; hi/items-1
	test	rdx, rdx
	jz	.locate_node_checkright
	cmp	r12, [rsi]
	cmovb	rdi, rdx
	jb	.locate_node
	je	.node_found
.locate_node_checkright:
	test	rcx, rcx
	jz	.node_found
	shl	r8d, 4
	cmp	r12, [rsi+r8]
	jbe	.node_found
	mov	rdi, rcx
	jmp	.locate_node
calign
.node_found:
	; find inner lower
	xor	r8d, r8d		; lo
	test	r10d, r10d		; hi
	jz	.node_found_highcheck
calign
.node_found_search:
	lea	edx, [r8d+r10d]
	shr	edx, 1			; mid = (lo+hi)>>1
	mov	eax, edx
	lea	ecx, [edx+1]		; mid+1
	lea	r11d, [edx-1]		; mid-1
	shl	eax, 4
	cmp	r12, [rsi+rax]
	cmovbe	r10d, r11d
	cmova	r8d, ecx
	cmp	r8d, r10d
	jl	.node_found_search
.node_found_highcheck:
	mov	eax, r10d
	lea	ecx, [r10d+1]
	cmp	r10d, 0
	cmovl	eax, ecx
	jl	.node_found_proceed
	shl	eax, 4
	cmp	[rsi+rax], r12
	cmovb	eax, ecx
	cmovae	eax, r10d
.node_found_proceed:
	; if eax == item count, past the end, return false
	mov	r8d, eax
	mov	edx, [r9+rdi+mtree_items_ofs]
	shl	r8d, 4
	cmp	eax, edx
	je	.return_false
	mov	[r13+mtree_iter_node_ofs], rdi
	mov	[r13+mtree_iter_pos_ofs], eax
	mov	eax, 1
	pop	r13 r12
	epilog
calign
.return_false:
	xor	eax, eax
	pop	r13 r12
	epilog

end if


if used mtree$upper_bound | defined include_everything
	; three arguments: rdi == mtree object, rsi == key, rdx == ptr to mtree_iter_size object we'll initialize
	; returns bool in eax as to whether we set the iterator to valid or not
	; (first key greater than the argument or end)
	; NOTE: since we support duplicate keys, this "cheats" and starts at lower bound and moves forward
	; TODO: someday when I am bored, do this w/ the correct search method (for my use cases the lazy way here is not a penalty)
	; NOTE 2: since this does actually walk, if we hit the actual end, we leave it valid such that a call to
	;   mtree_iter$prev results in a pointer to the last item, rather than wholly invalidating the iterator)
	;   and in this case, we return true (since the iterator is actually mtree$end)
falign
mtree$upper_bound:
	prolog	mtree$upper_bound
	push	rbx r12 r13
	mov	rbx, rdx
	mov	r12, rsi
	mov	r13, [rdi+mtree_base_ofs]
	call	mtree$lower_bound
	cmp	qword [rbx+mtree_iter_node_ofs], 0
	je	.return_false
	; since lower bound returns first key greater than or equal to our argument,
	; if this key is > our arg, we don't need to walk anywhere
	mov	rdx, [rbx+mtree_iter_node_ofs]
	mov	ecx, [rbx+mtree_iter_pos_ofs]
	mov	r8d, ecx
	mov	r11d, [r13+rdx+mtree_items_ofs]
	shl	r8d, 4
	sub	r11d, ecx
	add	r8, rdx
calign
.walk:
	cmp	[r13+r8+mtree_firstkey_ofs], r12
	ja	.return_true
	add	ecx, 1
	add	r8, 16
	sub	r11d, 1
	jnz	.walk
	; if we made it to here, we need to move to the next node
	; if there is _no_ next node, we still return true (since our iterator == mtree$end)
	cmp	qword [r13+rdx+mtree_next_ofs], 0
	je	.return_true
	mov	rdx, [r13+rdx+mtree_next_ofs]
	xor	ecx, ecx
	mov	r8, rdx
	jmp	.walk
calign
.return_true:
	mov	[rbx+mtree_iter_node_ofs], rdx
	mov	[rbx+mtree_iter_pos_ofs], ecx
	mov	eax, 1
	pop	r13 r12 rbx
	epilog
calign
.return_false:
	xor	eax, eax
	pop	r13 r12 rbx
	epilog

end if

	;
	;
	; SLOW ITERATOR-BASED FOREACH routines...
	;
	;    these are lazy implementations of the foreach variants
	;    TODO: someday when I am bored, these can be spedup by "inlining" a lot of the functionality called out from here
	;

if used mtree$foreach | defined include_everything
	; two arguments: rdi == mtree object, rsi == function to call (rdi == key, rsi == val)
falign
mtree$foreach:
	prolog	mtree$foreach
	push	rbx
	sub	rsp, mtree_iter_size
	mov	rbx, rsi
	mov	rsi, rsp
	call	mtree$begin
	cmp	qword [rsp+mtree_iter_node_ofs], 0
	je	.nothingtodo
calign
.loop:
	mov	rdi, rsp
	call	mtree_iter$key_value
	mov	rdi, rax
	mov	rsi, rdx
	call	rbx
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jnz	.loop
.nothingtodo:
	add	rsp, mtree_iter_size
	pop	rbx
	epilog

end if


if used mtree$foreach_arg | defined include_everything
	; three arguments: rdi == mtree object, rsi == function to call, rdx == arg to pass in rdi to functioon
	; to function, rsi == key, rdx == value
falign
mtree$foreach_arg:
	prolog	mtree$foreach_arg
	push	rbx r12
	sub	rsp, mtree_iter_size
	mov	rbx, rsi
	mov	r12, rdx
	mov	rsi, rsp
	call	mtree$begin
	cmp	qword [rsp+mtree_iter_node_ofs], 0
	je	.nothingtodo
calign
.loop:
	mov	rdi, rsp
	call	mtree_iter$key_value
	mov	rdi, r12
	mov	rsi, rax
	; rdx already set to value
	call	rbx
	mov	rdi, rsp
	call	mtree_iter$next
	test	eax, eax
	jnz	.loop
.nothingtodo:
	add	rsp, mtree_iter_size
	pop	r12 rbx
	epilog

end if


if used mtree$foreach_range | defined include_everything
	; four arguments: rdi == mtree_object, rsi == min key, rdx == max key, rcx == function to call (rdi == key, rsi == value)
	; min/max _inclusive_, will call for every key that satisfies >= min and <= max
	; (assumes sensible min/max)
falign
mtree$foreach_range:
	prolog	mtree$foreach_range
	push	rbx r12
	mov	rbx, rcx
	sub	rsp, mtree_iter_size * 2
	mov	r12, rdx
	mov	rdx, rsp
	call	mtree$lower_bound
	test	eax, eax
	jz	.nothingtodo
	; otherwise, init upper bound
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	mov	rsi, r12
	lea	rdx, [rsp+mtree_iter_size]
	call	mtree$upper_bound
	test	eax, eax
	jnz	.loop
	; otherwise, set it to the end
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree$end
calign
.loop:
	mov	rdi, rsp
	call	mtree_iter$key_value
	mov	rdi, rax
	mov	rsi, rdx
	call	rbx
	mov	rdi, rsp
	call	mtree_iter$next
	mov	rdi, rsp
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree_iter$compare
	test	eax, eax
	jz	.loop
.nothingtodo:
	add	rsp, mtree_iter_size * 2
	pop	r12 rbx
	epilog

end if


if used mtree$foreach_range_arg | defined include_everything
	; five arguments: rdi == mtree_object, rsi == min key, rdx == max key, rcx == function to call, r8 == arg to pass in rdi
	; min/max _inclusive_, will call for every key that satisfies >= min and <= max
	; will pass key in rsi, value in rdx, assumes sensible min/max
falign
mtree$foreach_range_arg:
	prolog	mtree$foreach_range_arg
	push	rbx r12 r13
	mov	rbx, rcx
	mov	r13, r8
	sub	rsp, mtree_iter_size * 2
	mov	r12, rdx
	mov	rdx, rsp
	call	mtree$lower_bound
	test	eax, eax
	jz	.nothingtodo
	; otherwise, init upper bound
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	mov	rsi, r12
	lea	rdx, [rsp+mtree_iter_size]
	call	mtree$upper_bound
	test	eax, eax
	jnz	.loop
	; otherwise, set it to the end
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree$end
calign
.loop:
	mov	rdi, rsp
	call	mtree_iter$key_value
	mov	rdi, r13
	mov	rsi, rax
	; value in rdx already in place
	call	rbx
	mov	rdi, rsp
	call	mtree_iter$next
	mov	rdi, rsp
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree_iter$compare
	test	eax, eax
	jz	.loop
.nothingtodo:
	add	rsp, mtree_iter_size * 2
	pop	r13 r12 rbx
	epilog

end if


if used mtree$foreach_reverse | defined include_everything
	; two arguments: rdi == mtree object, rsi == function to call (rdi == key, rsi == val)
	; same as foreach, but walks the tree backward instead
falign
mtree$foreach_reverse:
	prolog	mtree$foreach_reverse
	push	rbx
	sub	rsp, mtree_iter_size
	mov	rbx, rsi
	mov	rsi, rsp
	call	mtree$end
	cmp	qword [rsp+mtree_iter_node_ofs], 0
	je	.nothingtodo
calign
.loop:
	mov	rdi, rsp
	call	mtree_iter$prev
	test	eax, eax
	jz	.nothingtodo
	mov	rdi, rsp
	call	mtree_iter$key_value
	mov	rdi, rax
	mov	rsi, rdx
	call	rbx
	jmp	.loop
calign
.nothingtodo:
	add	rsp, mtree_iter_size
	pop	rbx
	epilog

end if


if used mtree$foreach_reverse_arg | defined include_everything
	; three arguments: rdi == mtree object, rsi == function to call, rdx == arg to pass in rdi
	; same as foreach_arg but walks the tree backward, passes key in rsi, val in rdx
falign
mtree$foreach_reverse_arg:
	prolog	mtree$foreach_reverse_arg
	push	rbx r12
	mov	r12, rdx
	sub	rsp, mtree_iter_size
	mov	rbx, rsi
	mov	rsi, rsp
	call	mtree$end
	cmp	qword [rsp+mtree_iter_node_ofs], 0
	je	.nothingtodo
calign
.loop:
	mov	rdi, rsp
	call	mtree_iter$prev
	test	eax, eax
	jz	.nothingtodo
	mov	rdi, rsp
	call	mtree_iter$key_value
	mov	rdi, r12
	mov	rsi, rax
	; value in rdx already in place
	call	rbx
	jmp	.loop
calign
.nothingtodo:
	add	rsp, mtree_iter_size
	pop	r12 rbx
	epilog

end if


if used mtree$foreach_range_reverse | defined include_everything
	; four arguments: rdi == mtree object, rsi == min key, rdx == max key, rcx == function to call (rdi == key, rsi == value)
	; min/max _inclusive_, will call for every key that satisfies >= min and <= max
	; same as foreach_range only walks the range backward instead
falign
mtree$foreach_range_reverse:
	prolog	mtree$foreach_range_reverse
	push	rbx r12
	mov	rbx, rcx
	sub	rsp, mtree_iter_size * 2
	mov	r12, rdx
	mov	rdx, rsp
	call	mtree$lower_bound
	test	eax, eax
	jz	.nothingtodo
	; otherwise, init upper bound
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	mov	rsi, r12
	lea	rdx, [rsp+mtree_iter_size]
	call	mtree$upper_bound
	test	eax, eax
	jnz	.loop
	; otherwise, set it to the end
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree$end
calign
.loop:
	; make sure our iterators are not equal, otherwise we are done
	mov	rdi, rsp
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree_iter$compare
	test	eax, eax
	jnz	.nothingtodo
	; move our trailing iterator back one
	lea	rdi, [rsp+mtree_iter_size]
	call	mtree_iter$prev
	lea	rdi, [rsp+mtree_iter_size]
	call	mtree_iter$key_value
	mov	rdi, rax
	mov	rsi, rdx
	call	rbx
	jmp	.loop
calign
.nothingtodo:
	add	rsp, mtree_iter_size * 2
	pop	r12 rbx
	epilog

end if


if used mtree$foreach_range_reverse_arg | defined include_everything
	; five arguments: rdi == mtree object, rsi == min key, rdx == max key, rcx == function to call, r8 == arg to pass in rdi
	; passes r8 in rdi, key in rsi, value in rdx
	; min/max _inclusive_, will call for every key that satisfies >= min and <= max
	; same as foreach_range_reverse but with arg in rdi
falign
mtree$foreach_range_reverse_arg:
	prolog	mtree$foreach_range_reverse_arg
	push	rbx r12 r13
	mov	rbx, rcx
	mov	r13, r8
	sub	rsp, mtree_iter_size * 2
	mov	r12, rdx
	mov	rdx, rsp
	call	mtree$lower_bound
	test	eax, eax
	jz	.nothingtodo
	; otherwise, init upper bound
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	mov	rsi, r12
	lea	rdx, [rsp+mtree_iter_size]
	call	mtree$upper_bound
	test	eax, eax
	jnz	.loop
	; otherwise, set it to the end
	mov	rdi, [rsp+mtree_iter_tree_ofs]
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree$end
calign
.loop:
	; make sure our iterators are not equal, otherwise we are done
	mov	rdi, rsp
	lea	rsi, [rsp+mtree_iter_size]
	call	mtree_iter$compare
	test	eax, eax
	jnz	.nothingtodo
	; move our trailing iterator back one
	lea	rdi, [rsp+mtree_iter_size]
	call	mtree_iter$prev
	lea	rdi, [rsp+mtree_iter_size]
	call	mtree_iter$key_value
	mov	rdi, r13
	mov	rsi, rax
	; value in rdx already in place
	call	rbx
	jmp	.loop
calign
.nothingtodo:
	add	rsp, mtree_iter_size * 2
	pop	r13 r12 rbx
	epilog

end if

	;
	; "cheater" mapped heap goods that sit inside the first page for
	; storing and retrieving integers (that obviously persist along with
	; the map).
	; The maximum index is mtree_root_size / 8 (or 0..5)
	;

if used mtree$getint | defined include_everything
	; two arguments: rdi == mtree object, esi == index 0..(mtree_root_size / 8)
	; returns whatever the stored integer is at the specified index
falign
mtree$getint:
	prolog	mtree$getint
	mov	rcx, [rdi+mtree_base_ofs]
	mov	rax, [rcx+rsi*8+((mtree_maxroot-2) * mtree_root_size)]
	epilog

end if


if used mtree$setint | defined include_everything
	; three arguments: rdi == mtree object, esi == index 0..(mtree_root_size / 8), rdx == value
	; sets the specified integer at index to value (does NOT bounds check)
falign
mtree$setint:
	prolog	mtree$setint
	mov	rax, [rdi+mtree_base_ofs]
	mov	[rax+rsi*8+((mtree_maxroot-2) * mtree_root_size)], rdx
	epilog

end if

	;
	;
	; "cheater" mapped heap goods that sit inside an mtree
	;
	;	thanks to allowing multiple roots in the same mapped object,
	;	we can also allow arbitrary space grabs around otherwise
	;	valid nodes
	;
	; we use the setroot at mtree_maxroot to hold a freelist tree
	; and a _second_ tree at mtree_maxroot-1 to hold a reversed freelist tree
	; one by size, the other by location, this makes non-tagged coalescing
	; possible.
	;
	; Every spot returned by this places its actual allocation size in the
	; preceding 8 bytes, such that given an offset only, we can determine
	; its size and thus when it is freed, reuse the space.
	;
	; this is not terribly efficient, but for persistent heap suits my use
	; cases nicely.
	;
	; NOTE: of course, if you use alloc/free, then you can't use the last
	; two roots in your userspace goods because we assume them here, so your
	; user-space mtree_maxroot is actually -2, and if you don't call alloc/free
	; then none of this code or the end two trees ever gets touched.
	; In other words, this is an entirely optional use of mtree functionality.
	;
	; And if you use getint or setint, they use -2 which puts your mtree_maxroot
	; at -3 instead of -2.
	;

if used mtree$alloc | defined include_everything
	; two arguments: rdi == mtree object, rsi == size of allocation required
	; returns -offset- in rax to allocated space.
falign
mtree$alloc:
	prolog	mtree$alloc
	; all allocations here need to include an extra 8 bytes, and be 16 aligned.
	add	rsi, 0x17	; 8 bytes for pointer preface, 15 bytes for alignment
	push	rbx r12 r13 r14
	and	rsi, not 0xf	; align 16
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13d, [rdi+mtree_current_root_ofs]	; save whatever the current root offset is
	mov	r14d, [rbx+mtree_settings_ofs]		; save whatever settings were in effect
	mov	dword [rbx+mtree_settings_ofs], mtree_multi	; make sure multiple keys are allowed
.reentry:
	mov	esi, mtree_maxroot - 1		; by size tree
	call	mtree$setroot
	sub	rsp, mtree_iter_size
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rsp
	call	mtree$lower_bound
	test	eax, eax
	jz	.freshspace	; if we didn't get a lower bound, fresh space is required
	; otherwise, we got a freelist item >= the size we want
	mov	rdi, rsp
	call	mtree_iter$key_value
	; rax == the size it is, rdx == offset it is
	; we are done with the iterator at rsp
	; current root is still set to our size tree
	mov	rdi, rsp
	push	rax rdx
	call	mtree_iter$delete
	pop	rdx rax
	mov	[rsp], rdx		; hangon to our offset
	mov	[rsp+8], rax		; hangon to our size too
	; delete the pointer from the other list
	mov	rdi, rbx
	mov	esi, mtree_maxroot		; by offset tree
	call	mtree$setroot
	mov	rdi, rbx
	mov	rsi, [rsp]		; offset
	call	mtree$delete
	mov	rcx, [rsp+8]
	sub	rcx, r12
	; if the space left < 32, don't bother splitting it up
	cmp	rcx, 32
	jb	.return_nosplit
	; otherwise, we have >= 32 bytes left in this free block
	; add the pointer and size to the offset tree
	mov	rax, [rsp]		; offset of entire block
	mov	[rsp+16], rcx
	; our root is still the offset tree
	mov	rdi, rbx
	lea	rsi, [rax+r12]		; offset of block remainder
	mov	rdx, rcx		; its size
	call	mtree$insert
	mov	rdi, rbx
	mov	esi, mtree_maxroot-1	; by size tree
	call	mtree$setroot
	; add the size and pointer to the size tree
	mov	rax, [rsp]		; offset of entire block
	mov	rdi, rbx
	mov	rsi, [rsp+16]		; the remainder size
	lea	rdx, [rax+r12]		; offset of block remainder
	call	mtree$insert
	; so now we can restore tree settings, and return
	; restore the root to whatever it was before we were called
	mov	[rbx+mtree_current_root_ofs], r13d
	mov	[rbx+mtree_settings_ofs], r14d	; restore settings
	mov	rdx, [rbx+mtree_base_ofs]
	mov	rax, [rsp]		; offset
	mov	[rdx+rax], r12		; new size preface
	add	rax, 8			; return.
	add	rsp, mtree_iter_size
	pop	r14 r13 r12 rbx
	epilog
calign
.return_nosplit:
	; restore the root to whatever it was before we were called
	mov	[rbx+mtree_current_root_ofs], r13d
	mov	[rbx+mtree_settings_ofs], r14d	; restore settings
	mov	rdx, [rbx+mtree_base_ofs]
	mov	rax, [rsp]		; offset
	mov	rcx, [rsp+8]		; its size
	; store its size in the first 8 bytes
	mov	[rdx+rax], rcx
	add	rax, 8			; return.
	add	rsp, mtree_iter_size
	pop	r14 r13 r12 rbx
	epilog
calign
.freshspace:
	; we "cheat" here despite it being a bit more work
	; everything in the mtree underlying mapped object must be mtree_nodesize aligned
	; so we align up to the nodesize of the request size, add it to both trees, and then
	; reenter, knowing that it can then proceed as normal
	mov	rax, r12
	add	rax, mtree_nodesize - 1
	and	rax, not (mtree_nodesize - 1)
	mov	[rsp], rax
	cmp	rax, mtree_nodesize
	je	.freshspace_checkfreelist
	; otherwise, a normal grow is required here.
.freshspace_grow:
	mov	rdi, [rbx+mtree_map_ofs]
	mov	rsi, rax
	call	mapped$grow
	mov	rdi, [rbx+mtree_map_ofs]
	mov	rsi, [rdi+mapped_base_ofs]
	sub	rax, rsi		; offset to the block we just got
.freshspace_gotit:
	mov	[rsp+8], rax
	; now we can add it to our blocks
	mov	[rbx+mtree_base_ofs], rsi	; save new base if we got remapped
	; setroot is still set to by size tree
	mov	rdi, rbx
	mov	rsi, [rsp]		; size of the block
	mov	rdx, [rsp+8]		; offset of the block
	call	mtree$insert
	mov	rdi, rbx
	mov	esi, mtree_maxroot	; by offset tree
	call	mtree$setroot
	mov	rdi, rbx
	mov	rsi, [rsp+8]		; offset of the block	
	mov	rdx, [rsp]		; size of the block
	call	mtree$insert
	; now we can unwind our goods and go back up
	add	rsp, mtree_iter_size
	mov	rdi, rbx
	jmp	.reentry
calign
.freshspace_checkfreelist:
	; the size requested also happens to be a normal tree node size
	; and since an mtree keeps all trees' free nodes in the single list at 0
	; we can remove one from there if it exists
	mov	rsi, [rbx+mtree_base_ofs]
	cmp	qword [rsi+mtree_free_ofs], 0
	je	.freshspace_grow
	; otherwise, there is one there so we can use it
	mov	rax, [rsi+mtree_free_ofs]
	mov	rcx, [rsi+rax]		; ptr to the next free node
	mov	[rsi+mtree_free_ofs], rcx
	; so now rax is free to use, and its size == mtree_nodesize
	jmp	.freshspace_gotit

end if


if used mtree$free | defined include_everything
	; two arguments: rdi == mtree object, rsi == offset of space being returned
falign
mtree$free:
	prolog	mtree$free
	sub	rsi, 8		; actual offset
	push	rbx r12 r13 r14
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13d, [rdi+mtree_current_root_ofs]	; save whatever the current root offset is
	mov	r14d, [rbx+mtree_settings_ofs]		; save whatever settings were in effect
	mov	dword [rbx+mtree_settings_ofs], mtree_multi	; make sure multiple keys are allowed
.again:
	mov	esi, mtree_maxroot			; by offset tree
	call	mtree$setroot
	
	; see if we can coalesce with our predecessor first up
	sub	rsp, mtree_iter_size
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rsp
	call	mtree$lower_bound
	test	eax, eax
	jz	.doinsert		; no offset is >= to ourself
	; otherwise, go back one
	mov	rdi, rsp
	call	mtree_iter$prev
	test	eax, eax
	jz	.noprev
	mov	rdi, rsp
	call	mtree_iter$key_value
	; rax is pointer, rdx is size
	lea	rcx, [rax+rdx]
	cmp	rcx, r12
	jne	.noprev

	; otherwise, we can coalesce our previous
	; remove our prior
	push	rax rdx
	; [rsp] == size
	; [rsp+8] == offset
	mov	rdi, rbx
	mov	rsi, rax	; offset of prior
	call	mtree$delete

	; find our size based iterator that matches, and delete it too
	mov	rdi, rbx
	mov	esi, mtree_maxroot-1			; by size tree
	call	mtree$setroot
	mov	rdi, rbx
	mov	rsi, [rsp]	; size
	lea	rdx, [rsp+16]	; iterator spot we can reuse
	call	mtree$lower_bound
	; we -know- that is valid.
	; while its pointer != ours, walk forward
calign
.prevsizeremove:
	lea	rdi, [rsp+16]	; iterator
	call	mtree_iter$key_value
	; rax == size, rdx == offset
	cmp	rdx, [rsp+8]	; offset
	je	.prevsizelocated
	lea	rdi, [rsp+16]	; iterator
	call	mtree_iter$next
	jmp	.prevsizeremove
calign
.prevsizelocated:
	lea	rdi, [rsp+16]	; iterator
	call	mtree_iter$delete

	; load up our size
	mov	rsi, [rbx+mtree_base_ofs]
	mov	rax, [rsp+8]	; offset of previous block
	mov	rcx, [rsi+r12]	; size of the one we are freeing
	add	[rsp], rcx	; new size of previous offset

	mov	rdx, [rsp]	; new size

	; set the new size
	mov	[rsi+rax], rdx	; new size

	; set our new r12 to rax	 (and we'll free that one too)
	mov	r12, rax
	pop	r9 r8		; not interested in previous ones anymore
	add	rsp, mtree_iter_size
	mov	rdi, rbx
	jmp	.again

calign
.noprev:
	; so either we couldn't go back one, or we went back one and its next wasn't us.
	; we know lower_bound is valid for us, so redo that
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rsp
	call	mtree$lower_bound
	mov	rdi, rsp
	call	mtree_iter$key_value
	; rax == offset/pointer, rdx == size
	; get our size and see if r12+it == rax
	mov	rsi, [rbx+mtree_base_ofs]
	mov	rcx, [rsi+r12]		; size of our block
	add	rcx, r12		; + our offset
	cmp	rcx, rax		; == next one in the free list?
	jne	.doinsert
	; otherwise, it does equal, so we can coalesce the next block to our own
	; new size of our block we can just add rdx to
	add	[rsi+r12], rdx

	; now we have to remove the size and offset of the next from the list and go back for more
	mov	rdi, rsp
	push	rax rdx
	; [rsp] == size
	; [rsp+8] == offset
	call	mtree_iter$delete	; delete offset based entry

	; switch to size based trew
	mov	rdi, rbx
	mov	esi, mtree_maxroot-1	; by size tree
	call	mtree$setroot

	mov	rdi, rbx
	mov	rsi, [rsp]		; size of next block
	lea	rdx, [rsp+16]
	call	mtree$lower_bound
	; we know that one is here that matches
calign
.nextsizeremove:
	lea	rdi, [rsp+16]
	call	mtree_iter$key_value
	cmp	rdx, [rsp+8]
	je	.nextsizelocated
	lea	rdi, [rsp+16]
	call	mtree_iter$next
	jmp	.nextsizeremove
calign
.nextsizelocated:
	lea	rdi, [rsp+16]	; iterator
	call	mtree_iter$delete

	; we already added the next's size to our own, and r12 is still valid
	pop	r9 r8		; not interested in previous ones anymore
	add	rsp, mtree_iter_size
	mov	rdi, rbx
	jmp	.again

calign
.doinsert:
	; rsp still a valid iterator space, but we are done with it
	add	rsp, mtree_iter_size
	mov	rdi, rbx
	mov	esi, mtree_maxroot			; by offset tree
	call	mtree$setroot
	; get our size
	mov	rsi, [rbx+mtree_base_ofs]
	mov	rdx, [rsi+r12]
	; insert our offset+size into the offset tree:
	mov	rdi, rbx
	mov	rsi, r12
	; rdx already set to our size
	push	rdx
	call	mtree$insert

	; insert our size + offset into the size tree
	mov	rdi, rbx
	mov	esi, mtree_maxroot-1			; by size tree
	call	mtree$setroot
	mov	rdi, rbx
	pop	rsi
	mov	rdx, r12
	call	mtree$insert

	; so now we can restore tree settings, and return
	; restore the root to whatever it was before we were called
	mov	[rbx+mtree_current_root_ofs], r13d
	mov	[rbx+mtree_settings_ofs], r14d	; restore settings
	pop	r14 r13 r12 rbx
	epilog

end if


if used mtree$read | defined include_everything
	; four arguments: rdi == mtree object, rsi == offset, rdx == buffer, rcx == count
	; this is a convenience wrapper for using mtree$alloc/mtree$free and lets you
	; read from inside the map and copy out to a buffer without having to dereference
	; yourself, though really: mtree_base_ofs == your actual base anyway, so this is
	; mainly here for a placeholder:
falign
mtree$read:
	prolog	mtree$read
	add	rsi, [rdi+mtree_base_ofs]
	mov	rdi, rdx
	mov	rdx, rcx
	call	memcpy
	epilog

end if


if used mtree$write | defined include_everything
	; four arguments: rdi == mtree object, rsi == offset, rdx == buffer, rcx == count
	; same as mtree$read, placeholder only, see comments above
falign
mtree$write:
	prolog	mtree$write
	add	rsi, [rdi+mtree_base_ofs]
	mov	rdi, rsi
	mov	rsi, rdx
	mov	rdx, rcx
	call	memcpy
	epilog

end if


if used mtree$alloc_clone | defined include_everything
	; three arguments: rdi == mtree object, rsi == source offset, rdx == source length
	; returns a newly mtree$alloc'd offset that contains a copy of rsi for rdx bytes
	; (useful to avoid having to double-buffer or hand-code yourself)
falign
mtree$alloc_clone:
	prolog	mtree$alloc_clone
	push	rdi rsi rdx
	mov	rsi, rdx
	call	mtree$alloc
	pop	rdx rsi rdi
	mov	rdi, [rdi+mtree_base_ofs]
	add	rsi, rdi
	add	rdi, rax
	push	rax
	call	memcpy
	pop	rax
	epilog

end if
