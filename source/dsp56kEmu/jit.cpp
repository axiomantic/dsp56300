#include "jit.h"

#include "dsp.h"
#include "jitblock.h"
#include "jitdspmode.h"
#include "jitprofilingsupport.h"
#include "jitblockemitter.h"

#include "asmjit/core/jitruntime.h"

#define WAIT_FOR_PROFILER 0

using namespace asmjit;

namespace dsp56k
{
#ifndef __ANDROID__
	// TODO: a.equals(b) is not a constant expression, android toolchain says. Maybe it needs an update?
	namespace
	{
		template<typename A, typename B> constexpr bool checkOverlap(const A& _a, const B& _b)
		{
			for (const auto& a : _a)
			{
				for (const auto& b : _b)
				{
					if (a.equals(b))
						return true;
				}
			}
			return false;
		}

		// pool registers are handed out front to back, so every volatile must be listed before the first non-volatile
		template<typename A, typename B> constexpr bool volatilesFirst(const A& _pool, const B& _nonVolatiles)
		{
			bool seenNonVolatile = false;

			for (const auto& p : _pool)
			{
				bool isNonVolatile = false;

				for (const auto& nv : _nonVolatiles)
				{
					if (p.equals(nv))
					{
						isNonVolatile = true;
						break;
					}
				}

				if (isNonVolatile)
					seenNonVolatile = true;
				else if (seenNonVolatile)
					return false;
			}
			return true;
		}

		template<typename A, typename B> constexpr bool contains(const A& _a, const B& _b)
		{
			for (const auto& a : _a)
			{
				if (a.equals(_b))
					return true;
			}
			return false;
		}

		static_assert(!checkOverlap(g_dspPoolGps, g_regGPTemps), "GP temp registers must not overlap with GP pool registers");
		static_assert(!contains(g_dspPoolXmms, regXMMTempA), "XMM temp registers must not overlap with XMM pool registers");
		static_assert(!contains(g_dspPoolXmms, regLastModAlu), "XMM temp registers must not contain XMM that holds the last modified ALU reg");
		static_assert(!contains(g_dspPoolGps, regDspPtr), "GP pool registers must not contain GP that holds the dsp register pointer");
		static_assert(!contains(g_dspPoolGps, regReturnVal), "GP pool registers must not contain GP that is used as scratch register");
		static_assert(!checkOverlap(g_funcArgGPs, g_regGPTemps), "GP temp registers must not overlap with function argument GPs");
		static_assert(!checkOverlap(g_funcArgGPs, g_nonVolatileGPs), "function argument registers cannot be non-volatile");

		// these are important as we do not have to push anything on the stack for simple functions if we can use volatiles only
		static_assert(!contains(g_nonVolatileGPs, *g_regGPTemps.begin()), "first temp must be volatile");
		static_assert(contains(g_nonVolatileGPs, regDspPtr), "register for DSP pointer must be non-volatile");
		static_assert(!contains(g_nonVolatileGPs, g_dspPoolGps[0]), "first pool reg must be volatile");
		static_assert(volatilesFirst(g_dspPoolGps, g_nonVolatileGPs), "GP pool registers must list all volatiles before the first non-volatile");
		static_assert(volatilesFirst(g_dspPoolXmms, g_nonVolatileXMMs), "XMM pool registers must list all volatiles before the first non-volatile");
	}
#endif
	constexpr bool g_traceOps = false;

	void funcCreate(JitDspPtr* _jit, const TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->create(_pc, true);
	}

	void funcRecreate(JitDspPtr* _jit, const TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->recreate(_pc);
	}

	void funcRunCheckPMemWrite(JitDspPtr* _jit, const TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->runCheckPMemWrite(_pc);
	}

	void funcRunCheckModeChange(JitDspPtr* _jit, const TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->runCheckModeChange(_pc);
	}

	void funcRunCheckPMemWriteAndModeChange(JitDspPtr* _jit, const TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->runCheckPMemWriteAndModeChange(_pc);
	}

	void funcRunCheckLoopEnd(JitDspPtr* _jit, const TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->runCheckLoopEnd(_pc);
	}

	void funcRunCheckLoopEndAndModeChange(JitDspPtr* _jit, const TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->runCheckLoopEndAndModeChange(_pc);
	}

	void funcRun(JitDspPtr* _jit, TWord _pc) noexcept
	{
		Jit::toJitPtr(_jit)->run(_pc);
	}

	Jit::Jit(DSP& _dsp) : m_dsp(_dsp), m_trampoline(_dsp), m_rt(new JitRuntime())
	{
		m_emitters.reserve(16);
		m_blockRuntimeDatas.reserve(0x10000);

#if WAIT_FOR_PROFILER
		// Wait for VTune profiler, somehow it does not immediately say that it is present
		const auto now = std::chrono::system_clock::now();
		while( std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - now) < std::chrono::milliseconds(10) )
		{
			if(JitProfilingSupport::isBeingProfiled())
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
#endif

		if (JitProfilingSupport::isBeingProfiled())
		{
			LOG("Detected profiler, generating additional data for it");
			try
			{
				m_profiling.reset(new JitProfilingSupport(m_dsp));
			}
			catch (const std::exception& e)
			{
				LOG("Failed to create profiler: " << e.what());
				m_profiling.reset();
			}
		}
		else
		{
			LOG("No profiler detected");
		}

		m_trampoline.generateCode();
	}

	Jit::~Jit()
	{
		m_chains.clear();

		for (const auto& emitter : m_emitters)
			delete emitter;
		for(const auto& rt : m_blockRuntimeDatas)
			delete rt;

		m_emitters.clear();
		m_blockRuntimeDatas.clear();

		delete m_rt;
	}

	void Jit::create(TWord _pc, bool _execute)
	{
		m_currentChain->create(_pc, _execute);
	}

	void Jit::recreate(TWord _pc)
	{
		m_currentChain->recreate(_pc);
	}

	void Jit::addLoop(const JitBlockInfo& _info)
	{
		if(_info.loopBegin != g_invalidAddress && _info.loopEnd != g_invalidAddress)
			addLoop(_info.loopBegin, _info.loopEnd);
	}

	TWord Jit::activeLoopBegin(const TWord _candidate) const noexcept
	{
		const auto& regs = m_dsp.regs();

		if(!(regs.sr.var & SR_LF))
			return g_invalidAddress;

		// Every DO form carries an address extension word (opcodeinfo.h marks the five Do_*
		// AbsoluteAddressExt and the four Dor_* PCRelativeAddressExt), so the DO that opened
		// a loop always sits two words before the loop's first instruction, which is the
		// address it stacked. JitBlock::getInfo identifies a loop body the same way.
		//
		// The running loop's frame is not necessarily on top: a JSR or a long interrupt
		// taken inside the loop stacks its own return address above it, and the write to LA
		// that brings us here may well be in that subroutine. Walking down to the first
		// frame that names a loop finds the innermost one, which is the one LA belongs to. A
		// return address cannot be mistaken for a loop's: it would have to be the address
		// after a call occupying the two words the DO occupies.
		//
		// Membership of the loop table is what identifies a frame as a loop's, rather than
		// decoding the opcode: this runs for every block that is created, and decoding costs
		// a search of the opcode table. _candidate is the loop addLoop is in the middle of
		// adding, which the table does not hold yet.
		for(auto index = m_dsp.ssIndex(); index > 0; --index)
		{
			const auto stackedPc = hiword(regs.ss[index]).toWord();

			if(stackedPc < 2)
				continue;

			const auto begin = stackedPc - 2;

			if(begin == _candidate || m_loops.find(begin) != m_loops.end())
				return begin;
		}

		return g_invalidAddress;
	}

	void Jit::addLoop(TWord _begin, TWord _end)
	{
		// The operand of a DO names the end the loop STARTS with, not the end it has: LA is
		// a writable register and a guest may move a running loop's end at any time. The
		// interpreter compares the PC against the live register on every pass, so for the
		// loop that is running right now the register is what this table has to agree with.
		if(_begin == activeLoopBegin(_begin))
			_end = (static_cast<TWord>(m_dsp.regs().la.var) + 1) & 0xffffff;

		// duplicated entries are allowed as the same code might be generated in multiple chains because it is run in different DSP modes. But in this case, the loop end must be identical
		const auto itBegin = m_loops.find(_begin);

		if(itBegin != m_loops.end())
		{
			// The end recorded for a loop may legitimately differ from the one another chain
			// derives for it: a guest may have moved it while the loop ran, and the entry
			// outlives the run. Whichever of the two is stale, the DO that re-enters the loop
			// reconciles the table against the live register, so keep what is here rather
			// than deciding between them now.
			assert(m_loopEnds.find(itBegin->second) != m_loopEnds.end());
			return;
		}

		assert(m_loopEnds.find(_end) == m_loopEnds.end());

		m_loops.insert(std::make_pair(_begin, _end));
		m_loopEnds.insert(_end);
	}

	void Jit::removeLoop(const JitBlockInfo& _info)
	{
		if(_info.loopBegin != g_invalidAddress)
			removeLoop(_info.loopBegin);
	}

	void Jit::removeLoop(const TWord _begin)
	{
		const auto it = m_loops.find(_begin);

		// multiple chains might have contained the loop, if the first chain removed it already, it is already gone
		if(it == m_loops.end())
			return;

		assert(m_loopEnds.find(it->second) != m_loopEnds.end());

		m_loopEnds.erase(it->second);
		m_loops.erase(it);
	}

	void Jit::destroy(TWord _pc)
	{
		for (auto& it : m_chains)
			it.second->destroy(_pc);
	}

	void Jit::destroyToRecreate(TWord _pc)
	{
		for (auto& it : m_chains)
			it.second->destroyToRecreate(_pc);
	}

	Jit* Jit::toJitPtr(DspRegs* _regs)
	{
		const auto offsetRegs = offsetof(DSP, reg);
		const auto offsetJit = offsetof(DSP, m_jit);

		return reinterpret_cast<Jit*>(reinterpret_cast<uint8_t*>(_regs) + offsetJit - offsetRegs);
	}

	void Jit::notifyProgramMemWrite(const TWord _offset)
	{
		for (auto& it : m_chains)
			it.second->notifyPMemWrite(_offset, it.second.get() == m_currentChain);

		m_maxUsedPAddress = std::max(m_maxUsedPAddress, static_cast<size_t>(_offset));
	}

	void Jit::run(const TWord _pc) noexcept
	{
		const auto* block = m_currentChain->getBlockUnsafe(_pc);
		m_trampoline.execOne(&m_dsp.regs(), _pc, block->getFunc());

		if(g_traceOps && m_dsp.m_trace)
		{
			const TWord lastPC = _pc + block->getPMemSize() - block->getLastOpSize();
			TWord op, opB;
			m_dsp.mem.getOpcode(lastPC, op, opB);
			m_dsp.traceOp(lastPC, op, opB, block->getLastOpSize());

			// make the diff tool happy, interpreter traces two ops. For the sake of simplicity, just trace it once more
			if (block->getDisasm().find("rep ") == 0)
				m_dsp.traceOp(lastPC, op, opB, block->getLastOpSize());
		}
	}

	void Jit::runCheckPMemWrite(const TWord _pc) noexcept
	{
		m_runtimeData.m_pMemWriteAddress = g_pcInvalid;
		run(_pc);
		checkPMemWrite();
	}

	void Jit::runCheckPMemWriteAndModeChange(const TWord _pc) noexcept
	{
		runCheckPMemWrite(_pc);
		checkModeChange();
	}

	void Jit::runCheckModeChange(const TWord _pc) noexcept
	{
		run(_pc);
		checkModeChange();
	}

	void Jit::runCheckLoopEnd(const TWord _pc) noexcept
	{
		run(_pc);
		checkLoopEnd();
	}

	void Jit::runCheckLoopEndAndModeChange(const TWord _pc) noexcept
	{
		runCheckLoopEnd(_pc);
		checkModeChange();
	}

	JitConfig Jit::getConfig(const TWord _pc) const
	{
		auto& globalConfig = getConfig();
		if(!globalConfig.getBlockConfig)
			return globalConfig;
		auto localConfig = globalConfig.getBlockConfig(_pc);
		if(localConfig)
			return *localConfig;
		return globalConfig;
	}

	void Jit::resetHW()
	{
		checkModeChange();
	}

	TJitFunc Jit::updateRunFunc(const JitCacheEntry& e)
	{
		const auto& i = e.block->getInfo();

		if(i.terminationReason == JitBlockInfo::TerminationReason::WritePMem)
		{
			if(i.hasFlag(JitBlockInfo::Flags::ModeChange))
				return &funcRunCheckPMemWriteAndModeChange;
			return &funcRunCheckPMemWrite;
		}

		// A block that writes LA may have moved the end of a loop that is still running, and
		// a DO re-entering a loop whose end was moved on an earlier run has to put the table
		// back. Any write to LA ends its block, so this is exactly the blocks that end on a
		// DO or on a write to LA, and nothing else. Selecting a wrapper here also takes the
		// block out of the set that can be reached by a direct jump from a parent, which is
		// what makes the check unskippable.
		if(any(i.writtenRegs, RegisterMask::LA))
		{
			if(i.hasFlag(JitBlockInfo::Flags::ModeChange))
				return &funcRunCheckLoopEndAndModeChange;
			return &funcRunCheckLoopEnd;
		}

		if(i.hasFlag(JitBlockInfo::Flags::ModeChange))
			return &funcRunCheckModeChange;

		if constexpr(g_traceOps)
		{
			if(e.block->getFunc())
				return &funcRun;
		}

		return e.block->getFunc();
	}

	void Jit::checkPMemWrite() noexcept
	{
		// if JIT code has written to P memory, destroy a JIT block if present at the write location
		const TWord pMemWriteAddr = m_runtimeData.m_pMemWriteAddress;

		if (pMemWriteAddr == g_pcInvalid)
			return;

		for (const auto& it : m_chains)
		{
			if (it.second->getBlock(pMemWriteAddr))
			{
				m_volatileP.insert(pMemWriteAddr);
				break;
			}
		}

		notifyProgramMemWrite(pMemWriteAddr);
		m_dsp.notifyProgramMemWrite(pMemWriteAddr);
	}

	void Jit::checkLoopEnd() noexcept
	{
		const auto begin = activeLoopBegin(g_invalidAddress);

		if(begin == g_invalidAddress)
			return;

		const auto it = m_loops.find(begin);

		if(it == m_loops.end())
			return;

		const auto end = (static_cast<TWord>(m_dsp.regs().la.var) + 1) & 0xffffff;
		const auto oldEnd = it->second;

		if(oldEnd == end)
			return;

		// The old end is baked into the boundary and the epilogue of every block built for
		// the body, so those blocks have to go. The block holding the DO itself must not:
		// it is what keeps this loop in the table, and a body block that does not find its
		// loop there never closes its back edge.
		removeLoop(begin);

		const auto last = oldEnd > end ? oldEnd : end;

		for(auto pc = begin + 2; pc < last; ++pc)
			destroy(pc);

		addLoop(begin, end);
	}

	void Jit::checkModeChange() noexcept
	{
		JitDspMode mode;

		mode.initialize(dsp());

		if(m_currentChain && m_currentChain->getMode() == mode)
			return;

//		LOG("DSP mode change to " << HEX(mode.get()));

		const auto itExisting = m_chains.find(mode);

		if(itExisting == m_chains.end())
		{
			m_currentChain = new JitBlockChain(*this, mode, m_maxUsedPAddress);
			m_chains.insert(std::make_pair(mode, m_currentChain));
		}
		else
		{
			m_currentChain = itExisting->second.get();
			m_currentChain->setMaxUsedPAddress(m_maxUsedPAddress);
		}

		m_dsp.setJitEntries(m_currentChain->getFuncs().data(), static_cast<TWord>(m_currentChain->getFuncs().size()));
	}

	void Jit::onDebuggerAttached(DebuggerInterface& _debugger) const
	{
		for (auto& it : m_chains)
		{
			const auto mode = it.first;
			auto* chain = (it.second).get();

			const auto pSize = m_dsp.memory().sizeP();

			const JitBlockRuntimeData* last = nullptr;

			for(TWord pc=0; pc<pSize; ++pc)
			{
				const auto* block = chain->getBlock(pc);
				if(block != last && block)
					_debugger.onJitBlockCreated(mode, block);
				last = block;
			}
		}
	}

	void Jit::destroyAllBlocks()
	{
		m_chains.clear();
		m_currentChain = nullptr;
		checkModeChange();
	}

	JitBlockEmitter* Jit::acquireEmitter(JitConfig&& _config)
	{
		if(m_emitters.empty())
			return new JitBlockEmitter(dsp(), getRuntimeData(), std::move(_config));

		auto* emitter = m_emitters.back();
		m_emitters.pop_back();

		emitter->reset(std::move(_config));

		return emitter;
	}

	JitBlockEmitter* Jit::acquireEmitter(const TWord _pc)
	{
		return acquireEmitter(getConfig(_pc));
	}

	void Jit::releaseEmitter(JitBlockEmitter* _emitter)
	{
		m_emitters.emplace_back(_emitter);
	}

	JitBlockRuntimeData* Jit::acquireBlockRuntimeData()
	{
		if(m_blockRuntimeDatas.empty())
			return new JitBlockRuntimeData();

		auto* r = m_blockRuntimeDatas.back();
		m_blockRuntimeDatas.pop_back();
		r->reset();
		return r;
	}

	void Jit::releaseBlockRuntimeData(JitBlockRuntimeData* _b)
	{
		m_blockRuntimeDatas.push_back(_b);
	}

	void Jit::onFuncsResized(const JitBlockChain& _chain) const
	{
		if(&_chain == m_currentChain)
		{
			m_dsp.setJitEntries(_chain.getFuncs().data(), static_cast<TWord>(_chain.getFuncs().size()));
		}
	}
}
