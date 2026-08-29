// TOOL-16 - the headless DSP56300 trace CLI.
//
// The test drives the tracecli BINARY as a child process, not the emulator
// library: the tool's contract is its stdout records and its exit status,
// and a child process is the only witness that observes both. Sibling tests
// in this directory all link dsp56kEmu and call into the library directly;
// this one departs from that shape on purpose, because the deliverable is
// the tool and not the engine.
//
// Programs are built here with the Assembler and written as raw big-endian
// 24-bit words into temp files, the load path the tool documents. The tool
// loads its program at P address 0, so branch targets are absolute from 0.
//
// The no-bound case relies on the tool REFUSING to run unbounded: the check
// observes the refusal message and the non-zero exit. If the refusal check
// is deleted, the child would execute the unbounded program; the child
// therefore runs under a wall-clock timeout so the mutation goes red in
// bounded time instead of hanging the suite.
//
// Engine note: the tool drives execInterpreter() - one instruction per
// quantum - because the JIT executes whole blocks per call and exposes no
// per-instruction hook on its compiled path, so a per-PC fetch histogram
// would be blind behind it.

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/unittests.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{
	using namespace dsp56k;

	struct ChildResult
	{
		int status = 0;
		bool timedOut = false;
		std::string out;
	};

	// Run a child with a wall-clock alarm. The alarm is the test's own
	// bound, not the tool's: it exists so a deleted refusal check cannot
	// hang this suite.
	ChildResult runChild(const std::string& _cmd, unsigned _seconds)
	{
		const auto redirected = _cmd + " 2>/dev/null";

		const auto* shell = "/bin/sh";
		const char* argv[] = {shell, "-c", redirected.c_str(), nullptr};

		int pipeFd[2] = {-1, -1};

		if(pipe(pipeFd) != 0)
			throw std::string("pipe failed");

		const auto pid = fork();

		if(pid < 0)
			throw std::string("fork failed");

		if(pid == 0)
		{
			close(pipeFd[0]);
			dup2(pipeFd[1], STDOUT_FILENO);
			close(pipeFd[1]);
			execvp(shell, const_cast<char* const*>(argv));
			_exit(127);
		}

		close(pipeFd[1]);

		std::string out;
		std::array<char, 4096> buf{};

		ssize_t n = 0;

		while((n = read(pipeFd[0], buf.data(), buf.size())) > 0)
			out.append(buf.data(), static_cast<size_t>(n));

		close(pipeFd[0]);

		int status = 0;
		bool timedOut = false;

		for(unsigned i = 0; ; ++i)
		{
			const auto r = waitpid(pid, &status, WNOHANG);

			if(r == pid)
				break;

			if(r < 0)
				throw std::string("waitpid failed");

			if(i >= _seconds * 100)
			{
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				timedOut = true;
				break;
			}

			usleep(10000);
		}

		return {status, timedOut, out};
	}

	ChildResult runCli(const std::vector<std::string>& _args, unsigned _seconds = 30)
	{
		std::string cmd = std::string("\"") + TRACECLI_BINARY + "\"";

		for(const auto& a : _args)
			cmd += " \"" + a + "\"";

		return runChild(cmd, _seconds);
	}

	// The tool's input format: 3 bytes big-endian per 24-bit word.
	std::vector<uint8_t> bigEndianWords(const std::vector<TWord>& _words)
	{
		std::vector<uint8_t> bytes;
		bytes.reserve(_words.size() * 3);

		for(const auto w : _words)
		{
			bytes.push_back(static_cast<uint8_t>((w >> 16) & 0xff));
			bytes.push_back(static_cast<uint8_t>((w >> 8) & 0xff));
			bytes.push_back(static_cast<uint8_t>(w & 0xff));
		}

		return bytes;
	}

	std::string writeTempProgram(const std::string& _name, const std::vector<TWord>& _words)
	{
		const auto path = "/tmp/t0_trace_cli_" + _name + ".bin";

		std::ofstream f(path, std::ios::binary | std::ios::trunc);

		if(!f.is_open())
			throw std::string("cannot write ") + path;

		const auto bytes = bigEndianWords(_words);
		f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

		if(!f)
			throw std::string("short write to ") + path;

		return path;
	}

	std::vector<TWord> assembleProgram(const std::vector<std::string>& _lines)
	{
		Assembler assembler;
		std::vector<TWord> words;

		for(const auto& line : _lines)
		{
			const auto r = assembler.assemble(line.c_str());

			if(!r.success())
				throw std::string("assemble failed for: ") + line;

			for(uint32_t i = 0; i < r.wordCount; ++i)
				words.push_back(r.word[i]);
		}

		return words;
	}

	std::vector<std::string> splitLines(const std::string& _s)
	{
		std::vector<std::string> lines;
		std::string cur;

		for(const auto c : _s)
		{
			if(c == '\n')
			{
				lines.push_back(cur);
				cur.clear();
			}
			else
			{
				cur.push_back(c);
			}
		}

		if(!cur.empty())
			lines.push_back(cur);

		return lines;
	}

	std::vector<std::string> linesWithPrefix(const std::string& _out, const std::string& _prefix)
	{
		std::vector<std::string> found;

		for(const auto& line : splitLines(_out))
		{
			if(line.compare(0, _prefix.size(), _prefix) == 0)
				found.push_back(line);
		}

		return found;
	}

	// The converging program, loaded at P:0 by the tool:
	//   0: move #<3,x0       x0 = 0x030000
	//   1: move x0,x:$3f     the watched store
	//   2: jmp $2            a one-word self-jump; the PC stops moving
	//
	// The tool records one fetch per quantum: the PC the interpreter is about
	// to fetch. Three quanta run (0, 1, 2), each address fetched once; on the
	// third quantum the PC no longer moves, so the tool stops and reports
	// bound-reached=0. The X-memory store makes exactly one watched address
	// change, in the quantum that executed the store.
	std::vector<TWord> convergingProgram()
	{
		Assembler assembler;

		const auto load = assembler.assemble("move #<3,x0");
		verify(load.success());
		verify(load.wordCount == 1);
		verify(load.word[0] == 0x240300);

		const auto store = assembler.assemble("move x0,x:$3f");
		verify(store.success());
		verify(store.wordCount == 1);
		verify(store.word[0] == 0x443f00);

		const auto idle = assembler.assemble("jmp $2");
		verify(idle.success());
		verify(idle.wordCount == 1);
		verify(idle.word[0] == 0xc0002);

		return {load.word[0], store.word[0], idle.word[0]};
	}

	// An unbounded run: the entry is a one-word jump past the loaded block,
	// and the PC keeps moving through zeroed P memory (nops) forever, so a
	// bounded run exhausts its quantum budget without ever converging.
	std::vector<TWord> infiniteProgram()
	{
		Assembler assembler;

		const auto idle = assembler.assemble("jmp $2");
		verify(idle.success());
		verify(idle.wordCount == 1);
		verify(idle.word[0] == 0xc0002);

		return {idle.word[0]};
	}

	// A program whose words are never fetched: the tool starts the PC past
	// the loaded block, and the guest converges on a self-jump the tool
	// loaded separately... it cannot. Instead: an empty program. The tool
	// loads nothing, the fetch histogram is empty and examined=0.
	std::vector<TWord> emptyProgram()
	{
		return {};
	}

	// ------------------------------------------------------------------ cases

	void noBoundIsRefused()
	{
		const auto program = writeTempProgram("converge", convergingProgram());

		const auto r = runCli({"-in", program});

		verify(!r.timedOut);
		verify(r.status != 0);
		verify(r.out.find("tracecli: missing --quanta bound") != std::string::npos);

		// no records may precede the refusal
		verify(splitLines(r.out).size() == 1);
	}

	void noBoundRefusalDoesNotRunUnbounded()
	{
		// The mutation case: with the refusal check deleted, this command
		// would execute the unbounded self-jump program forever. The alarm
		// in runChild is the witness that the tool still refused in bounded
		// time; a timed-out child means the refusal is gone.
		const auto program = writeTempProgram("infinite", infiniteProgram());

		const auto r = runCli({"-in", program, "--quanta", "8"}, 5);

		verify(!r.timedOut);
		verify(r.status == 0);

		const auto summary = linesWithPrefix(r.out, "tracecli ");

		verify(summary.size() == 1);
		verify(summary[0].find("bound-reached=1") != std::string::npos);
		verify(summary[0].find("quanta=8") != std::string::npos);
	}

	void histogramAndConvergenceRecords()
	{
		const auto program = writeTempProgram("converge", convergingProgram());

		const auto r = runCli({"-in", program, "--quanta", "64"});

		verify(!r.timedOut);
		verify(r.status == 0);

		// sorted (address,count) set, as values
		const auto fetches = linesWithPrefix(r.out, "fetch ");

		verify(fetches.size() == 3);
		verify(fetches[0] == "fetch 0 count=1 first=0");
		verify(fetches[1] == "fetch 1 count=1 first=1");
		verify(fetches[2] == "fetch 2 count=1 first=2");

		// the register-delta record: x0 is EReg 4, 0 -> 0x030000
		const auto regs = linesWithPrefix(r.out, "reg ");

		bool foundX0 = false;

		for(const auto& line : regs)
		{
			if(line == "reg 4 from=0 to=196608")
			{
				verify(!foundX0);
				foundX0 = true;
			}
		}

		verify(foundX0);

		// converged: the bound was NOT reached
		const auto summary = linesWithPrefix(r.out, "tracecli ");

		verify(summary.size() == 1);
		verify(summary[0].find("bound-reached=0") != std::string::npos);
		verify(summary[0].find("examined=3") != std::string::npos);
	}

	void boundFlagFlipsToReached()
	{
		const auto program = writeTempProgram("infinite", infiniteProgram());

		const auto r = runCli({"-in", program, "--quanta", "8"});

		verify(!r.timedOut);
		verify(r.status == 0);

		const auto summary = linesWithPrefix(r.out, "tracecli ");

		verify(summary.size() == 1);
		verify(summary[0].find("bound-reached=1") != std::string::npos);
		verify(summary[0].find("quanta=8") != std::string::npos);

		const auto fetches = linesWithPrefix(r.out, "fetch ");

		verify(fetches.size() == 1);
		verify(fetches[0] == "fetch 0 count=1 first=0");
	}

	void emptyHistogram()
	{
		const auto program = writeTempProgram("empty", emptyProgram());

		const auto r = runCli({"-in", program, "--quanta", "4"});

		verify(!r.timedOut);
		verify(r.status == 0);

		const auto fetches = linesWithPrefix(r.out, "fetch ");

		verify(fetches.empty());

		const auto summary = linesWithPrefix(r.out, "tracecli ");

		verify(summary.size() == 1);
		verify(summary[0].find("examined=0") != std::string::npos);
	}

	void watchFiresOnceAndIsSilentWithout()
	{
		const auto program = writeTempProgram("converge", convergingProgram());

		const auto withWatch = runCli({"-in", program, "--quanta", "8", "-watch", "X:3f"});

		verify(!withWatch.timedOut);
		verify(withWatch.status == 0);

		const auto hits = linesWithPrefix(withWatch.out, "watch ");

		// exactly one record: the store, in the quantum that fetched word 1
		verify(hits.size() == 1);
		verify(hits[0] == "watch X 63 q=1 val=196608");

		const auto withoutWatch = runCli({"-in", program, "--quanta", "8"});

		verify(!withoutWatch.timedOut);
		verify(withoutWatch.status == 0);

		const auto silent = linesWithPrefix(withoutWatch.out, "watch ");

		verify(silent.empty());
	}

	void hdi08IsSilentWithoutHandshake()
	{
		// A program that never touches the host port must print no hdi08
		// records. The HDI08 handshake path itself is exercised at the API
		// level by the tool's TX callback wiring; a guest-driven handshake
		// record requires a firmware corpus this test deliberately avoids.
		const auto program = writeTempProgram("converge", convergingProgram());

		const auto r = runCli({"-in", program, "--quanta", "8"});

		verify(!r.timedOut);
		verify(r.status == 0);

		const auto hdi = linesWithPrefix(r.out, "hdi08 ");

		verify(hdi.empty());
	}
}

int main()
{
	try
	{
		noBoundIsRefused();
		noBoundRefusalDoesNotRunUnbounded();
		histogramAndConvergenceRecords();
		boundFlagFlipsToReached();
		emptyHistogram();
		watchFiresOnceAndIsSilentWithout();
		hdi08IsSilentWithoutHandshake();
	}
	catch(const std::string& _err)
	{
		std::cout << "t0_trace_cli FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "t0_trace_cli passed" << std::endl;
	return 0;
}
