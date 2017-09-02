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
	;       
	; aesxts.inc: AES-256/XTS encryption/decryption
	; 
	; this is by the AESXTS standard, except we use two full sets of 32 byte keys
	; instead of half each
	;

if used aesxts$encrypt | used aesxts$decrypt | defined include_everything


; NOTE: this _must_ be a power of 2 >= 16
aesxts_blocksize = 4096

; 1 << this needs to equal the above
aesxts_blocksize_bits = 12

aesxts_blocks = aesxts_blocksize shr 4



; the size of our allocated structure requires three AES256 contexts:
aesxts_size = aes_size * 3


end if


if used aesxts$new | defined include_everything
	; two arguments: rdi == ptr to 32 byte key, rsi == ptr to 32 byte tweak key
falign
aesxts$new:
	prolog	aesxts$new
	push	rdi rsi
	mov	edi, aesxts_size
	call	heap$alloc
	pop	rdx rsi
	mov	rdi, rax
	push	rax
	call	aesxts$keys
	pop	rax
	epilog

end if


if used aesxts$keys | defined include_everything
	; three arguments: rdi == aesxts object, rsi == ptr to 32 byte key, rdx == ptr to 32 byte tweak key
falign
aesxts$keys:
	prolog	aesxts$keys
	push	rbx r12 r13
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13, rdx
	mov	edx, 32
	call	aes$init_encrypt
	lea	rdi, [rbx+aes_size]
	mov	rsi, r13
	mov	edx, 32
	call	aes$init_encrypt
	lea	rdi, [rbx+(aes_size*2)]
	mov	rsi, r12
	mov	edx, 32
	call	aes$init_decrypt
	pop	r13 r12 rbx
	epilog

end if


if used aesxts$destroy | defined include_everything
	; single argument in rdi: aesxts object
falign
aesxts$destroy:
	prolog	aesxts$destroy
	push	rdi
	xor	esi, esi
	mov	edx, aesxts_size
	call	memset32
	pop	rdi
	call	heap$free
	epilog

end if



if used aesxts$encrypt | defined include_everything
	; three arguments: rdi == aesxts object, rsi == ptr to aesxts_blocksize worth of plaintext, rdx == ptr to encryption tweak (e.g. blocknum) (must be 16 bytes in length)
falign
aesxts$encrypt:
	prolog	aesxts$encrypt
	push	rbx r12 r13 r14 r15
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13, rdx
	; first order of business: encrypt the tweak using the tweak encrypt aes context:
	lea	rdi, [rdi+aes_size]
	mov	rsi, rdx
	call	aes$encrypt
	; now, for aesxts_blocks, do our tweaked encrypt
	mov	r14d, aesxts_blocks
calign
.tweaked:
	; xor our block with the tweak
	mov	rax, [r13]
	mov	rcx, [r13+8]
	xor	[r12], rax
	xor	[r12+8], rcx
	; encrypt the block
	mov	rdi, rbx
	mov	rsi, r12
	call	aes$encrypt
	; xor with the tweak again
	mov	rax, [r13]
	mov	rcx, [r13+8]
	xor	[r12], rax
	xor	[r12+8], rcx
	; LSFR the tweak
	shl	rcx, 1
	jc	.lsfr_carry
	shl	rax, 1
	adc	rcx, 0
	mov	[r13], rax
	mov	[r13+8], rcx

	; proceed to next block or done
	add	r12, 16
	sub	r14d, 1
	jnz	.tweaked
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.lsfr_carry:
	shl	rax, 1
	adc	rcx, 0
	xor	rax, 0x87
	mov	[r13], rax
	mov	[r13+8], rcx
	
	; proceed to next block or done
	add	r12, 16
	sub	r14d, 1
	jnz	.tweaked
	pop	r15 r14 r13 r12 rbx
	epilog


end if


if used aesxts$decrypt | defined include_everything
	; three arguments: rdi == aesxts object, rsi == ptr to aesxts_blocksize worth of ciphertext, rdx == ptr to encryption tweak (e.g. blocknum)
falign
aesxts$decrypt:
	prolog	aesxts$decrypt
	push	rbx r12 r13 r14 r15
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13, rdx
	; first order of business: encrypt the tweak using the tweak encrypt aes context:
	lea	rdi, [rdi+aes_size]
	mov	rsi, rdx
	call	aes$encrypt
	; now, for aesxts_blocks, do our tweaked encrypt
	mov	r14d, aesxts_blocks
calign
.tweaked:
	; xor our block with the tweak
	mov	rax, [r13]
	mov	rcx, [r13+8]
	xor	[r12], rax
	xor	[r12+8], rcx
	; decrypt the block
	lea	rdi, [rbx+(aes_size*2)]
	mov	rsi, r12
	call	aes$decrypt
	; xor with the tweak again
	mov	rax, [r13]
	mov	rcx, [r13+8]
	xor	[r12], rax
	xor	[r12+8], rcx
	; LSFR the tweak
	shl	rcx, 1
	jc	.lsfr_carry
	shl	rax, 1
	adc	rcx, 0
	mov	[r13], rax
	mov	[r13+8], rcx

	; proceed to next block or done
	add	r12, 16
	sub	r14d, 1
	jnz	.tweaked
	pop	r15 r14 r13 r12 rbx
	epilog
calign
.lsfr_carry:
	shl	rax, 1
	adc	rcx, 0
	xor	rax, 0x87
	mov	[r13], rax
	mov	[r13+8], rcx
	
	; proceed to next block or done
	add	r12, 16
	sub	r14d, 1
	jnz	.tweaked

	pop	r15 r14 r13 r12 rbx
	epilog

end if
