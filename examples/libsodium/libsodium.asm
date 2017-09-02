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
	; libsodium.asm: A quick-and-dirty example of the HeavyThing's libsodium
	;                crypto_box_easy and crypto_box_easy_open functionality
	;       
	; first things first, include the library defaults, and the
	; library main include:
include '../../ht_defaults.inc'
include '../../ht.inc'

	; program entry point:
public _start
falign
_start:
	; every HeavyThing program needs to start with a call to initialise it
	call	ht$init


	; first up: we need two private keys, which would ordinarily be RNG-generated
	; but for our example (and for easy reproducing with the reference library)
	; we'll set them to specific values:

	mov	edi, 32
	call	heap$alloc
	mov	rbx, rax

	mov	rdi, rax
	mov	esi, 0x46
	mov	edx, 32
	call	memset		; private key #1 in rbx set to all 0x46

	mov	edi, 32
	call	heap$alloc
	mov	r12, rax

	mov	rdi, rax
	mov	esi, 0x47
	mov	edx, 32
	call	memset		; private key #2 in r12 set to all 0x47

	; next up, we need two public keys from each
	mov	edi, 32
	call	heap$alloc
	mov	r13, rax

	mov	edi, 32
	call	heap$alloc
	mov	r14, rax

	; use Curve25519 to get our public keys
	mov	rdi, r13	; public key #1
	mov	rsi, rbx	; private key #1
	call	curve25519$donna_basepoint

	mov	rdi, r14	; public key #2
	mov	rsi, r12	; private key #2
	call	curve25519$donna_basepoint

	; the space required for our ciphertext is 16 bytes + our plaintext length
	mov	edi, .msglen + 16
	call	heap$alloc
	mov	r15, rax	; r15 will hold our ciphertext

	; next up, we need a RNG-generated nonce
	mov	edi, 32
	call	heap$alloc_random
	mov	rbp, rax	; rbp holds our nonce

	; output that to stdout
	mov	rdi, .nonce_is
	call	string$to_stdout
	mov	rdi, rbp
	mov	esi, 32
	call	string$from_bintohex
	mov	rdi, rax
	push	rax
	call	string$to_stdoutln
	pop	rdi
	call	heap$free

	; next up: call crypto_box_easy
	; it requires:
	; rdi == destination buffer for ciphertext (plaintext length + 16 bytes in size)
	; rsi == plaintext
	; rdx == length of same
	; rcx == ptr to nonce
	; r8 == recipient public key
	; r9 == sender private key
	mov	rdi, r15
	mov	rsi, .msg
	mov	edx, .msglen
	mov	rcx, rbp
	mov	r8, r14		; public key #2 (recipient)
	mov	r9, rbx		; private key #1 (sender)
	call	crypto_box_easy

	; send that to stdout:
	mov	rdi, .ciphertext_is
	call	string$to_stdoutln
	mov	rdi, r15
	mov	esi, .msglen + 16
	call	string$from_bintohex
	mov	rdi, rax
	push	rax
	call	string$to_stdoutln
	pop	rdi
	call	heap$free

	; last but not least, perform our decryption with crypto_box_open_easy
	; it requires:
	; rdi == destination buffer for plaintext (ciphertext length - 16 bytes in size)
	; rsi == ciphertext
	; rdx == length of same
	; rcx == ptr to nonce
	; r8 == sender public key
	; r9 == recipient private key
	
	; for our simple example here, we can just allocate stack space for our
	; decrypted plaintext:
	sub	rsp, 256
	mov	rdi, rsp	; destination for plaintext
	mov	rsi, r15	; ciphertext buffer
	mov	edx, .msglen + 16	; length of same
	mov	rcx, rbp	; nonce
	mov	r8, r13		; public key #1 (sender's)
	mov	r9, r12		; private key #2 (recipient's)
	call	crypto_box_open_easy
	; that returns a bool in eax as to whether it succeeded or not:
	mov	rdi, rbp
	mov	ebp, eax
	call	heap$free
	mov	rdi, .decrypt_success
	mov	rsi, .decrypt_fail
	test	ebp, ebp
	cmovz	rdi, rsi
	call	string$to_stdoutln
	test	ebp, ebp
	jz	.bailout
	; otherwise, output the plaintext
	mov	eax, syscall_write
	mov	edi, 1
	mov	rsi, rsp
	mov	edx, .msglen ; (we know this to be its size from above)
	syscall

.bailout:
	; cleanup after ourselves:
	mov	rdi, rbx
	call	heap$free_clear

	mov	rdi, r12
	call	heap$free_clear

	mov	rdi, r13
	call	heap$free
	mov	rdi, r14
	call	heap$free
	mov	rdi, r15
	call	heap$free

	add	rsp, 256

	mov	eax, syscall_exit
	xor	edi, edi
	syscall
cleartext .nonce_is, 'nonce is: '
cleartext .ciphertext_is, 'ciphertext is: '
cleartext .decrypt_success, 'Decryption successful, plaintext is:'
cleartext .decrypt_fail, 'Decryption failed.'
dalign
.msg:
	db	'This is our libsodium example test message/plaintext.',10
.msglen = $ - .msg

	; include the global data segment:
include '../../ht_data.inc'

