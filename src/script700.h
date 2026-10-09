#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

class Script700
{
public:
	Script700();

	int compile(const char* source);
	int compile(const char* source, std::size_t length);
	void disable();
	void reset();
	void advance(std::uint64_t ticks);
	std::uint64_t waitTicks() const;
	bool enabled() const;
	float volume() const;
	bool sourceMuted(unsigned source) const;
	bool takeInputPortWrite(std::uint8_t& port, std::uint8_t& value);

	void attach(std::uint8_t* ram, std::uint8_t* inputPorts, std::uint8_t* outputPorts);
	const std::string& error() const;

private:
	enum class OperandType
	{
		Immediate,
		InputPort,
		OutputPort,
		Work,
		Ram8,
		Ram16,
		Ram32,
		Data8,
		Data16,
		Data32
	};

	struct Operand
	{
		OperandType type;
		std::uint32_t index;
		int compareIndex;

		Operand(OperandType operandType = OperandType::Immediate,
			std::uint32_t operandIndex = 0, int dynamicCompare = -1)
			: type(operandType), index(operandIndex), compareIndex(dynamicCompare) {}
	};

	enum class OpCode
	{
		Wait,
		Flush,
		FlushDisable,
		FlushEnable,
		Move,
		Compare,
		Add,
		Subtract,
		Multiply,
		Divide,
		ModuloSigned,
		ModuloUnsigned,
		And,
		Or,
		Xor,
		ShiftLeft,
		ShiftRightSigned,
		ShiftRightUnsigned,
		Not,
		Branch,
		BranchEqual,
		BranchNotEqual,
		BranchGreaterEqual,
		BranchLessEqual,
		BranchGreater,
		BranchLess,
		BranchCarryClear,
		BranchLower,
		BranchHigher,
		BranchCarrySet,
		Quit,
		Nop
	};

	struct Instruction
	{
		OpCode code;
		Operand first;
		Operand second;
		std::size_t target;
	};

	std::uint32_t read(const Operand& operand) const;
	void write(const Operand& operand, std::uint32_t value);
	void run();

	std::vector<Instruction> program_;
	std::vector<std::uint8_t> data_;
	std::uint32_t work_[8];
	std::uint32_t compare_[2];
	std::size_t pc_;
	std::uint64_t waitTicks_;
	bool enabled_;
	bool stopped_;
	bool flushEnabled_;
	bool flushWaiting_;
	std::uint8_t flushPorts_[4];
	std::uint8_t* ram_;
	std::uint8_t* inputPorts_;
	std::uint8_t* outputPorts_;
	std::string error_;
	float volume_;
	bool sourceMute_[256];
	std::deque<std::pair<std::uint8_t, std::uint8_t>> inputPortWrites_;
};
