//go:build nocturne_posix_host

package nocturne

/*
#include <nocturne/posix_host.h>
*/
import "C"

func init() { linked.posixHost = true }

type TerminalReport struct {
	InputTTY, OutputTTY, HostAvailable                              bool
	Interactive, UTF8, HalfBlocks, Braille, Pointer, BracketedPaste bool
	ColorMode, AmbiguousWidth, EmojiWidth                           uint32
}

// ProbeTerminal inspects the descriptors with the native POSIX capability probe.
func ProbeTerminal(inputFD, outputFD int32) (TerminalReport, error) {
	var out C.NtPosixTerminalReport
	err := check(C.nt_posix_terminal_probe(C.int(inputFD), C.int(outputFD), nil, &out))
	caps := out.capabilities
	return TerminalReport{
		InputTTY: bool(out.input_tty), OutputTTY: bool(out.output_tty), HostAvailable: bool(out.host_available),
		Interactive: bool(caps.interactive), UTF8: bool(caps.utf8), HalfBlocks: bool(caps.half_blocks),
		Braille: bool(caps.braille), Pointer: bool(caps.pointer), BracketedPaste: bool(caps.bracketed_paste),
		ColorMode: uint32(caps.color), AmbiguousWidth: uint32(caps.width.ambiguous), EmojiWidth: uint32(caps.width.emoji),
	}, err
}
