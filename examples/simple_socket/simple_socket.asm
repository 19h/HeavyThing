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
	; simple_socket.asm: A quick example of both host-based and IPv4 based
	; outbound socket communications (client-side).
	;
	; first things first, include the library defaults, and the
	; library main include:
include '../../ht_defaults.inc'
include '../../ht.inc'
	
	; all HeavyThing epoll-based goods are "virtual method table" based, which
	; means that in order to be useful, we need to "override" the library-
	; provided ones (the library provided ones do work, but do nothing by
	; design).
	; So, for this example, we copy the epoll$default_vtable (from epoll.inc)
	; and modify it to include our custom functions:
	; (note: the dalign macro aligns the table per the ht_defaults.inc setting)
dalign
example_vtable:
	; the original epoll$default_vtable looks like:
        ; dq      epoll$destroy, epoll$clone, io$connected, epoll$send, epoll$receive, io$error, io$timeout
	; our modified one:
        dq      epoll$destroy, epoll$clone, example_connected, epoll$send, example_receive, example_error, example_timeout


	; we will keep a global variable for our hostname argument and port number:
globals
{
	hostname	dq	0
	portnumber	dd	0
	; also, a boolean as to whether or not we successfully connected
	; (see notes below for DNS-based connection attempts)
	hasconnected	dd	0
}


	; and our function definitions:


	; this gets called [obviously] when/if the socket is actually connected and ready:
	; single argument gets passed in rdi: the epoll object
falign
example_connected:
	prolog	example_connected
	mov	dword [hasconnected], 1
	; hangon to our epoll object:
	push	rbx r12 r13
	mov	rbx, rdi
	; send a message to stderr indicating we are under way:
	mov	rdi, .connectmsg
	call	string$to_stderrln
	; so for our simple example, we'll do three string$concat ops
	; to form a very basic HTTP/1.0 request header:
	mov	rdi, .get1
	mov	rsi, [hostname]
	call	string$concat
	mov	r12, rax
	mov	rdi, rax
	mov	rsi, .get2
	call	string$concat
	mov	rdi, r12
	mov	r12, rax
	call	heap$free
	; so r12 now contains a string version of our request
	; but to send it out, we need UTF8:
	mov	rdi, r12
	call	string$utf8_length
	mov	rdi, rax
	call	heap$alloc
	mov	r13, rax
	mov	rdi, r12
	mov	rsi, rax
	call	string$to_utf8
	; that returns the # of bytes (same as utf8_length does)
	; so we can reuse that for our send operation:
	mov	rdi, rbx		; epoll object
	mov	rsi, r13		; UTF8 request text
	mov	edx, eax		; bytes to send
	call	epoll$send
	; cleanup after ourselves:
	mov	rdi, r13
	call	heap$free
	mov	rdi, r12
	call	heap$free
	pop	r13 r12 rbx
	epilog
cleartext .connectmsg, '[stderr]: Connected, sending HTTP/1.0 GET request.'
cleartext .get1, 'GET / HTTP/1.0',13,10,'Host: '
cleartext .get2, 13,10,13,10


	; this [obviously] gets called when data arrives
	; three arguments: rdi == epoll object, rsi == ptr to data, rdx == lenght of same
	; if we return 0 in eax, the epoll/socket stays alive, 1 in eax == destroys the epoll object
falign
example_receive:
	prolog	example_receive

	; some notes here: the epoll.inc functionality always "accumulates" data it receives
	; in a buffer object, and per the commentary that accompanies epoll$receive, we must
	; "drain" it (though as you might have guessed, there are cases when accumulating
	; by default is a nice feature)

	; all we want to do is send what we receive straight to stdout, and we don't need
	; the HeavyThing to do that:
	mov	eax, syscall_write
	push	qword [rdi+epoll_inbuf_ofs]
	mov	edi, 1		; fd == stdout
	; rsi already valid, so is rdx
	syscall
	pop	rdi
	; we don't want the epoll layer to accumulate, so reset it:
	call	buffer$reset
	; per the above commentary, we don't want the epoll layer to destroy us, return 0
	xor	eax, eax
	epilog




	; single argument gets passed in rdi: the epoll object
	; this is a notification only, the epoll object will get destroyed soon after this call
	; (this can happen both for actual errors and socket closes, we aren't interested in the
	; difference thanks to most actual errors being raised long before this happens)
falign
example_error:
	prolog	example_error

	; so for a non-DNS based lookup, this is our read error (or connect fail, etc)
	; for DNS based, this could mean that the lookup itself failed also (which is why we kept
	; track of hasconnected in the example_connected) function.

	cmp	dword [hasconnected], 0
	je	.baddns_or_connrefused

	; otherwise, just output something to stderr and be done:
	mov	rdi, .closed
	call	string$to_stderrln
	mov	eax, syscall_exit
	xor	edi, edi
	syscall
	epilog
cleartext .closed, '[stderr]: Connection closed'
cleartext .dns_or_refused, '[stderr]: DNS lookup failed or connection refused.'
calign
.baddns_or_connrefused:
	mov	rdi, .dns_or_refused
	call	string$to_stderrln
	mov	eax, syscall_exit
	mov	edi, 1		; exit status 1 for error
	syscall
	epilog


	; single argument in rdi: the epoll object
	; if we return false, object will stay alive, return 1 in eax == epoll object will get destroyed
falign
example_timeout:
	prolog	example_timeout
	; just bailout with a stderr message:
	mov	rdi, .timeoutmsg
	call	string$to_stderrln
	mov	eax, syscall_exit
	mov	edi, 1		; exit status 1 for error
	syscall
	epilog
cleartext .timeoutmsg, '[stderr]: Connection timed out.'


	
	; program entry point:
public _start
falign
_start:
	; every HeavyThing program needs to start with a call to initialise it
	call	ht$init

	; we expect to receive at minimum a hostname or IPv4 argument, possibly a port number
	cmp	dword [argc], 2
	jb	.usage
	cmp	dword [argc], 3
	ja	.usage
	; since [argv] is a list of strings, pop argv[0] off the front and get rid of it (our program name)
	mov	rdi, [argv]
	call	list$pop_front
	mov	rdi, rax
	call	heap$free
	; next arg is either a hostname, or an IPv4
	mov	rdi, [argv]
	call	list$pop_front
	; store that into our global defined above:
	mov	[hostname], rax
	mov	dword [portnumber], 80
	cmp	dword [argc], 3
	jne	.noportargument
	mov	rdi, [argv]
	call	list$pop_front	; pop_back would work of course here too
	; hang on to the string
	push	rax
	; convert that to an integer and make sure it is okay
	mov	rdi, rax
	call	string$to_int
	cmp	rax, 0
	jle	.badport
	cmp	rax, 65536
	jge	.badport
	mov	dword [portnumber], eax
	pop	rdi
	call	heap$free
.noportargument:
	; try and convert the hostname and port to an inet_addr...
	; we need an inet_addr worth of stackspace:
	sub	rsp, sockaddr_in_size		; 16 bytes for IPv4
						; note here for unix addresses (110 bytes), we'd want to
						; maintain stack alignment
	mov	rdi, rsp
	mov	rsi, [hostname]
	mov	edx, dword [portnumber]
	call	inet_addr
	test	eax, eax
	jz	.dns_based
	; inet_addr returned 1 for success, so now we can proceed with epoll goods

	mov	rdi, example_vtable		; our modified vtable from above
	xor	esi, esi			; no extra space required in the epoll object
	call	epoll$new
	; we need to set a timeout for connect, say 30 seconds for our example:
	mov	ecx, 30000
	; timeouts get specified in milliseconds, and we could call epoll$set_readtimeout
	; but it is a one-liner function so we'll just do it here directly:
	mov	[rax+epoll_readtimeout_ofs], rcx
	; epoll outbound needs rdi==address, esi==length of same, rdx == epoll object
	mov	rdi, rsp
	mov	esi, sockaddr_in_size		; 16 bytes for IPv4
	mov	rdx, rax
	call	epoll$outbound
	; so if that fails, connect() failed, so bailout here and now:
	test	eax, eax
	jz	.connectfail
	; otherwise, pass control to epoll$run (which never returns)
	call	epoll$run
	; not reached.
calign
.dns_based:
	; so if we get to here, inet_addr said our hostname argument isn't an IPv4
	; ... so we will use DNS to do our outbound.
	; first: undo our previous stack modification since we aren't dealing directly
	; with IPv4 sockaddr_in
	add	rsp, sockaddr_in_size
	;
	; some notes here: our example_error function will get called in the event of
	; a DNS lookup failure, and that is why we kept an additional global in this
	; example for whether or not we connected or not...
	; in a real world use case, we'd have either created an io.inc chain for our
	; epoll object, or requested additional space during epoll$new and used the
	; extra space to store our own custom variables that get carried around with
	; each connection/socket object... our simple example is a one-shot though
	; so we didn't bother with any of that.
	;
	; create an epoll object with our custom vtable:
	mov	rdi, example_vtable		; our modified vtable from above
	xor	esi, esi			; no extra space required in the epoll object
	call	epoll$new
	; we need to set a timeout for connect, say 30 seconds for our example:
	mov	ecx, 30000
	; timeouts get specified in milliseconds, and we could call epoll$set_readtimeout
	; but it is a one-liner function so we'll just do it here directly:
	mov	[rax+epoll_readtimeout_ofs], rcx
	; epoll$outbound_hostname needs string hostname, port, and epoll object:
	mov	rdi, [hostname]
	mov	esi, dword [portnumber]
	mov	rdx, rax
	call	epoll$outbound_hostname

	; pass control to epoll$run (which never returns)
	call	epoll$run
	; not reached.
cleartext .badportstr, 'Port specified is not between 1..65535.'
cleartext .connectfailstr, 'connect() failed.'
calign
.badport:
	mov	rdi, .badportstr
	jmp	.error_exit
.connectfail:
	mov	rdi, .connectfailstr
	jmp	.error_exit
calign
.usage:
	mov	rdi, .usagestr
.error_exit:
	call	string$to_stderrln
	mov	eax, syscall_exit
	mov	edi, 1
	syscall
cleartext .usagestr, 'usage: ./simple_socket hostname_or_IPv4 [port]',10,'  If port is not specified, defaults to 80.'

	; include the global data segment:
include '../../ht_data.inc'

