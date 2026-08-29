// tracecli - headless DSP56300 trace CLI.
//
// Loads a raw 24-bit-word P-memory program, runs a BOUNDED number of quanta
// and prints machine-readable per-run records, one per line, as key=value
// pairs. A run without an explicit --quanta bound is refused: an unbounded
// DSP program does not terminate.
//
// Execution is driven through DSP::execInterpreter(), one instruction per
// call. The JIT executes whole blocks per call and exposes no per-instruction
// hook on its compiled path, so per-PC fetch observation would be blind
// behind it; the interpreter is the engine the fetch histogram can actually
// see. Registers and memory are read through the public DSP surface.
//
// Output lines:
//   fetch <addr> count=<n> first=<q>      P-memory fetch histogram, sorted by address;
//                                         one fetch = the PC about to execute, per quantum
//   reg <index> from=<v> to=<v>           register-file deltas, index over EReg
//   hdi08 <event> q=<q>                   HDI08 handshake transitions (TX callback)
//   watch <area> <addr> q=<q> val=<v>     watch-list hits: a watched address whose value
//                                         changed during a quantum, sampled before and
//                                         after the quantum; address is decimal
//   tracecli quanta=<n> bound-reached=<0|1> examined=<P addresses fetched>
//
// Convergence: the run stops early when a quantum leaves the PC unchanged in the default
// processing mode, and bound-reached reports 0. A run that exhausts its quantum budget
// reports 1.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/registers.h"

#include "dsp56kBase/logging.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using namespace dsp56k;

	constexpr uint32_t g_frameRate96k = 96000;

	struct WatchSpec
	{
		EMemArea area = MemArea_X;
		TWord addr = 0;
	};

	bool parseArea(const std::string& _area, EMemArea& _dst)
	{
		if(_area == "X" || _area == "x")
		{
			_dst = MemArea_X;
			return true;
		}
		if(_area == "Y" || _area == "y")
		{
			_dst = MemArea_Y;
			return true;
		}
		return false;
	}

	bool parseTWord(const std::string& _text, TWord& _dst)
	{
		if(_text.empty())
			return false;

		// hex unless a plain decimal is spelled out; the whole string must parse
		size_t pos = 0;

		try
		{
			unsigned long v;

			if(_text.size() > 2 && _text[0] == '0' && (_text[1] == 'x' || _text[1] == 'X'))
				v = std::stoul(_text, &pos, 16);
			else if(_text.find_first_not_of("0123456789") == std::string::npos)
				v = std::stoul(_text, &pos, 10);
			else
				v = std::stoul(_text, &pos, 16);

			if(pos != _text.size() || v > 0xffffff)
				return false;

			_dst = static_cast<TWord>(v);
			return true;
		}
		catch(const std::exception&)
		{
			return false;
		}
	}

	bool loadProgramFile(std::vector<TWord>& _dst, const std::string& _filename, bool _bigEndian)
	{
		std::ifstream file(_filename, std::ios::binary | std::ios::in);

		if(!file.is_open())
			return false;

		std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

		// an empty file is a valid empty program: nothing is loaded and no
		// fetch can land inside the program range

		// ASCII hex text (as the disassembler accepts it) is also a program
		// format this tool takes; everything else is treated as raw bytes.
		bool isAscii = !buffer.empty();

		for(const auto byte : buffer)
		{
			const bool ascii = ((byte >= '0' && byte <= '9') ||
				(byte >= 'A' && byte <= 'F') ||
				(byte >= 'a' && byte <= 'f') ||
				byte == ' ' || byte == '\n' || byte == '\r' || byte == '\t');

			if(!ascii)
			{
				isAscii = false;
				break;
			}
		}

		if(isAscii)
		{
			std::string hex;

			for(const auto byte : buffer)
			{
				if(byte == ' ' || byte == '\n' || byte == '\r' || byte == '\t')
					continue;
				hex.push_back(static_cast<char>(byte >= 'A' && byte <= 'F' ? byte + 'a' - 'A' : byte));
			}

			if(hex.size() / 6 * 6 != hex.size())
				return false;

			const auto toNibble = [](char _c)
			{
				if(_c >= '0' && _c <= '9')
					return _c - '0';
				return _c - 'a' + 0xa;
			};

			buffer.resize(hex.size() >> 1);

			size_t h = 0;
			for(auto& b : buffer)
			{
				auto byte = toNibble(hex[h++]) << 4;
				byte |= toNibble(hex[h++]);
				b = static_cast<uint8_t>(byte);
			}
		}

		if(buffer.size() / 3 * 3 != buffer.size())
			return false;

		_dst.resize(buffer.size() / 3);

		size_t s = 0;

		for(TWord& d : _dst)
		{
			const TWord a = buffer[s++];
			const TWord b = buffer[s++];
			const TWord c = buffer[s++];

			if(_bigEndian)
				d = (a << 16) | (b << 8) | c;
			else
				d = (c << 16) | (b << 8) | a;
		}

		return true;
	}

	// A DebuggerInterface subclass is how the existing non-GUI debugger half
	// observes the core (dsp56kDebugger/debugger.h implements the same
	// interface). The interface is attached through DSP::setDebugger so the
	// tool drives the core the same way the debugger does. One caveat shapes
	// the watch implementation: the per-write onMemoryWrite hook is compiled
	// out of the dsp56kEmu library unless the whole library is built with
	// DSP56300_DEBUGGER=1, so watches are polled instead - the watched
	// addresses are sampled before and after every quantum and a value
	// change emits one record. The record names the quantum in which the
	// value changed and the new value; multiple writes inside one quantum
	// collapse to the last one.
	class TraceRecorder final : public DebuggerInterface
	{
	public:
		struct HdilEvent
		{
			TWord q = 0;
			std::string event;
		};

		struct WatchEvent
		{
			TWord q = 0;
			EMemArea area = MemArea_X;
			TWord addr = 0;
			TWord value = 0;
		};

		explicit TraceRecorder(DSP& _dsp) : DebuggerInterface(_dsp)
		{
		}

		void onProgramMemWrite(TWord _addr) override
		{
			m_fetchCounts[_addr]++;
			m_fetchFirst.emplace(_addr, m_quantum);
		}

		// called by the run loop BEFORE a quantum executes
		void sampleWatches()
		{
			for(const auto& w : m_watches)
				m_watchPrev[w.area][w.addr] = dsp().memory().get(w.area, w.addr);
		}

		// called by the run loop AFTER a quantum executes
		void collectWatchChanges()
		{
			for(const auto& w : m_watches)
			{
				const auto now = dsp().memory().get(w.area, w.addr);

				if(now != m_watchPrev[w.area][w.addr])
					m_watchEvents.push_back({m_quantum, w.area, w.addr, now});
			}
		}

		void setQuantum(const TWord _q)
		{
			m_quantum = _q;
		}

		void setWatches(const std::vector<WatchSpec>& _watches)
		{
			m_watches = _watches;
		}

		void setHdiCallback()
		{
			auto* p = dynamic_cast<Peripherals56311*>(dsp().getPeriph(0));

			if(!p)
				return;

			p->getHDI08().setWriteTxCallback([this]()
			{
				m_hdiEvents.push_back({m_quantum, "tx"});
			});
		}

		const auto& fetchCounts() const { return m_fetchCounts; }
		const auto& fetchFirst() const { return m_fetchFirst; }
		const auto& hdiEvents() const { return m_hdiEvents; }
		const auto& watchEvents() const { return m_watchEvents; }

	private:
		TWord m_quantum = 0;
		std::vector<WatchSpec> m_watches;
		std::map<TWord, TWord> m_fetchCounts;
		std::map<TWord, TWord> m_fetchFirst;
		std::vector<HdilEvent> m_hdiEvents;
		std::vector<WatchEvent> m_watchEvents;
		std::map<EMemArea, std::map<TWord, TWord>> m_watchPrev;
	};

	void printUsage()
	{
		std::cout << "DSP 56300 trace CLI" << std::endl;
		std::cout << std::endl;
		std::cout << "Usage:" << std::endl;
		std::cout << "tracecli -in programfile --quanta n [options]" << std::endl;
		std::cout << std::endl;
		std::cout << "Options:" << std::endl;
		std::cout << "-in filename       Input program file, required. Raw big-endian 24-bit words (binary), or ASCII hex text." << std::endl;
		std::cout << "--quanta n         Maximum number of instructions to execute, required." << std::endl;
		std::cout << "-pc aabbcc         Program start address in hex. Defaults to 0." << std::endl;
		std::cout << "le                 Input file is little-endian. Default is big-endian." << std::endl;
		std::cout << "-watch area:addr   Observe an X or Y memory address, area is X or Y, addr in hex. Repeatable." << std::endl;
		std::cout << "-vba aabbcc        Interrupt vector base in hex. Defaults to 0." << std::endl;
		std::cout << std::endl;
		std::cout << "Output: one record per line, key=value pairs. See the file header comment for the record format." << std::endl;
	}

}	// namespace

int main(int _argc, char* _argv[])
{
	// Every output this tool prints is a named record. Library diagnostics
	// (JIT assembly dumps, MMU notices) are prose and would corrupt the
	// record stream, so the log sink is replaced before anything runs.
	Logging::setLogFunc([](const std::string&){});

	std::string inFile;
	bool hasIn = false;
	TWord quanta = 0;
	bool hasQuanta = false;
	TWord pc = 0;
	TWord vba = 0;
	bool littleEndian = false;
	std::vector<WatchSpec> watches;

	for(int i = 1; i < _argc; ++i)
	{
		const std::string arg(_argv[i]);

		if(arg == "--quanta" && i + 1 < _argc)
		{
			if(!parseTWord(_argv[++i], quanta))
			{
				std::cout << "tracecli: invalid --quanta value" << std::endl;
				return 1;
			}
			hasQuanta = true;
			continue;
		}

		if((arg == "-in" || arg == "--in") && i + 1 < _argc)
		{
			inFile = _argv[++i];
			hasIn = true;
			continue;
		}

		if((arg == "-pc" || arg == "--pc") && i + 1 < _argc)
		{
			if(!parseTWord(_argv[++i], pc))
			{
				std::cout << "tracecli: invalid -pc value" << std::endl;
				return 1;
			}
			continue;
		}

		if((arg == "-vba" || arg == "--vba") && i + 1 < _argc)
		{
			if(!parseTWord(_argv[++i], vba))
			{
				std::cout << "tracecli: invalid -vba value" << std::endl;
				return 1;
			}
			continue;
		}

		if((arg == "-watch" || arg == "--watch") && i + 1 < _argc)
		{
			const std::string spec(_argv[++i]);
			const auto split = spec.find(':');

			EMemArea area = MemArea_X;

			if(split == std::string::npos || !parseArea(spec.substr(0, split), area) || !parseTWord(spec.substr(split + 1), watches.emplace_back().addr))
			{
				std::cout << "tracecli: invalid -watch spec " << spec << ", expected area:addr with area X or Y" << std::endl;
				return 1;
			}

			watches.back().area = area;
			continue;
		}

		if(arg == "-le" || arg == "--le")
		{
			littleEndian = true;
			continue;
		}

		std::cout << "tracecli: unknown argument " << arg << std::endl;
		return 1;
	}

	if(!hasQuanta)
	{
		// An unbounded DSP program does not terminate. The refusal is the
		// tool's contract, not a validation nicety.
		std::cout << "tracecli: missing --quanta bound" << std::endl;
		return 1;
	}

	if(!hasIn)
	{
		printUsage();
		return 1;
	}

	std::vector<TWord> program;

	if(!loadProgramFile(program, inFile, !littleEndian))
	{
		std::cout << "tracecli: failed to load program file " << inFile << std::endl;
		return 1;
	}

	DefaultMemoryValidator validator;
	Memory memory(validator, 0x080000, 0x800000, 0x200000);
	Peripherals56311 periph(g_frameRate96k);
	DSP dsp(memory, &periph, &periph.ySpace());

	TraceRecorder recorder(dsp);
	dsp.setDebugger(&recorder);

	// Mask interrupts: the 56311 ESAI raises its transmit-data-empty condition
	// from construction and the vector table this tool loads is empty. The
	// caller can still unmask via guest code.
	const_cast<TReg24&>(dsp.getSR()).var |= (SR_I0 | SR_I1);

	if(vba)
		dsp.regs().vba = TReg24(vba);

	for(size_t i = 0; i < program.size(); ++i)
		dsp.memWriteP(pc + static_cast<TWord>(i), program[i]);

	recorder.setWatches(watches);
	recorder.setHdiCallback();

	dsp.setPC(pc);

	const auto pcStart = pc;
	const auto wordCount = static_cast<TWord>(program.size());

	std::map<EReg, int64_t> regFrom;

	for(uint32_t r = 0; r < Reg_COUNT; ++r)
	{
		int64_t v = 0;
		if(dsp.readRegToInt(static_cast<EReg>(r), v))
			regFrom[static_cast<EReg>(r)] = v;
	}

	TWord executed = 0;
	bool boundReached = false;
	bool converged = false;

	for(TWord q = 0; q < quanta; ++q)
	{
		recorder.setQuantum(q);
		recorder.sampleWatches();

		const auto pcBefore = dsp.getPC().toWord();

		if(pcBefore >= pcStart && pcBefore < pcStart + wordCount)
			recorder.onProgramMemWrite(pcBefore);

		dsp.execInterpreter();
		++executed;

		const auto pcAfter = dsp.getPC().toWord();

		recorder.collectWatchChanges();

		if(pcAfter == pcBefore && dsp.getProcessingMode() == DSP::Default)
		{
			// A converged run: the guest sits on a self-jump whose PC no
			// longer moves. Report the convergence and stop.
			converged = true;
			break;
		}
	}

	boundReached = !converged;

	// register-file deltas, in EReg order
	for(const auto& kv : regFrom)
	{
		int64_t vTo = 0;

		if(!dsp.readRegToInt(kv.first, vTo))
			continue;

		if(vTo == kv.second)
			continue;

		std::cout << "reg " << static_cast<int>(kv.first)
			<< " from=" << kv.second
			<< " to=" << vTo << std::endl;
	}

	// fetch histogram, sorted by address
	for(const auto& kv : recorder.fetchCounts())
	{
		TWord first = 0;

		const auto it = recorder.fetchFirst().find(kv.first);

		if(it != recorder.fetchFirst().end())
			first = it->second;

		std::cout << "fetch " << kv.first
			<< " count=" << kv.second
			<< " first=" << first << std::endl;
	}

	for(const auto& e : recorder.hdiEvents())
		std::cout << "hdi08 " << e.event << " q=" << e.q << std::endl;

	for(const auto& e : recorder.watchEvents())
		std::cout << "watch " << (e.area == MemArea_X ? "X" : "Y")
			<< " " << e.addr
			<< " q=" << e.q
			<< " val=" << e.value << std::endl;

	std::cout << "tracecli quanta=" << executed
		<< " bound-reached=" << (boundReached ? 1 : 0)
		<< " examined=" << recorder.fetchCounts().size() << std::endl;

	dsp.setDebugger(nullptr);

	return 0;
}
