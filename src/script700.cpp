#include "script700.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace
{
	std::string trim(const std::string& value)
	{
		const std::string::size_type first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
			return std::string();
		const std::string::size_type last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	std::string lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	bool parseNumber(const std::string& text, std::uint32_t& value)
	{
		if (text.empty())
			return false;

		const char* begin = text.c_str();
		int base = 10;
		if (text[0] == '$')
		{
			begin++;
			base = 16;
		}
		else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
		{
			base = 16;
		}
		char* end = nullptr;
		const unsigned long result = std::strtoul(begin, &end, base);
		if (!end || *end != '\0')
			return false;
		value = static_cast<std::uint32_t>(result);
		return true;
	}
}

Script700::Script700()
	: pc_(0), waitTicks_(0), enabled_(false), stopped_(true), ram_(nullptr),
	  inputPorts_(nullptr), outputPorts_(nullptr), volume_(1.0f)
{
	std::fill(work_, work_ + 8, 0);
	std::fill(compare_, compare_ + 2, 0);
	std::fill(sourceMute_, sourceMute_ + 256, false);
}

void Script700::attach(std::uint8_t* ram, std::uint8_t* inputPorts, std::uint8_t* outputPorts)
{
	ram_ = ram;
	inputPorts_ = inputPorts;
	outputPorts_ = outputPorts;
}

void Script700::disable()
{
	program_.clear();
	data_.clear();
	volume_ = 1.0f;
	std::fill(sourceMute_, sourceMute_ + 256, false);
	enabled_ = false;
	reset();
}

void Script700::reset()
{
	std::fill(work_, work_ + 8, 0);
	std::fill(compare_, compare_ + 2, 0);
	pc_ = 0;
	waitTicks_ = 0;
	inputPortWrites_.clear();
	stopped_ = !enabled_;
}

bool Script700::enabled() const
{
	return enabled_;
}

float Script700::volume() const
{
	return volume_;
}

bool Script700::sourceMuted(unsigned source) const
{
	return sourceMute_[source & 255];
}

const std::string& Script700::error() const
{
	return error_;
}

std::uint64_t Script700::waitTicks() const
{
	return waitTicks_;
}

bool Script700::takeInputPortWrite(std::uint8_t& port, std::uint8_t& value)
{
	if (inputPortWrites_.empty())
		return false;
	port = inputPortWrites_.front().first;
	value = inputPortWrites_.front().second;
	inputPortWrites_.pop_front();
	return true;
}

int Script700::compile(const char* source)
{
	if (!source)
	{
		disable();
		error_.clear();
		return 0;
	}
	return compile(source, std::strlen(source));
}

int Script700::compile(const char* source, std::size_t length)
{
	struct ParsedLine
	{
		std::size_t number;
		std::string command;
		std::vector<std::string> arguments;
	};

	std::vector<ParsedLine> lines;
	std::unordered_map<std::uint32_t, std::size_t> labels;
	std::istringstream stream{ std::string(source, length) };
	std::string sourceLine;
	std::size_t lineNumber = 0;
	int zone = 0;
	std::vector<std::uint8_t> parsedData;
	float parsedVolume = 1.0f;
	bool parsedSourceMute[256] = {};

	while (std::getline(stream, sourceLine))
	{
		++lineNumber;
		// Some compatible hosts pass a text buffer without a trailing NUL.
		// Treat binary control data following the script as the end of input.
		if (std::find_if(sourceLine.begin(), sourceLine.end(), [](unsigned char c)
			{
				return c < 0x20 && c != '\t' && c != '\r';
			}) != sourceLine.end())
			break;
		const std::string::size_type comment = sourceLine.find(';');
		if (comment != std::string::npos)
			sourceLine.erase(comment);
		sourceLine = trim(sourceLine);
		if (sourceLine.empty())
			continue;

		if (sourceLine == "e" || sourceLine == "E" || sourceLine == "::")
		{
			if (zone < 2)
				++zone;
			continue;
		}
		if (zone == 1)
		{
			std::istringstream extension(sourceLine);
			std::string command;
			std::string target;
			std::string valueText;
			std::uint32_t value = 0;
			extension >> command >> target >> valueText;
			if (lower(command) == "v" && target == "!" && parseNumber(valueText, value))
				parsedVolume = static_cast<float>(value) / 65536.0f;
			else if (lower(command) == "m")
			{
				if (target == "!")
					for (bool& muted : parsedSourceMute)
						muted = !muted;
				else if (parseNumber(target, value))
					parsedSourceMute[value & 255] = !parsedSourceMute[value & 255];
			}
			continue;
		}
		if (zone == 2)
		{
			if (sourceLine[0] == ':' || sourceLine[0] == '#')
				continue;
			std::istringstream dataTokens(sourceLine);
			std::string token;
			while (dataTokens >> token)
			{
				if (token.size() & 1)
					continue;
				for (std::size_t i = 0; i < token.size(); i += 2)
				{
					char byteText[3] = { token[i], token[i + 1], 0 };
					char* end = nullptr;
					const unsigned long byte = std::strtoul(byteText, &end, 16);
					if (end && *end == '\0')
						parsedData.push_back(static_cast<std::uint8_t>(byte));
				}
			}
			continue;
		}

		if (sourceLine[0] == ':')
		{
			const std::string::size_type labelEnd = sourceLine.find_first_of(" \t", 1);
			const std::string labelText = sourceLine.substr(1,
				labelEnd == std::string::npos ? std::string::npos : labelEnd - 1);
			std::uint32_t label = 0;
			if (!parseNumber(labelText, label))
			{
				error_ = "Invalid label at line " + std::to_string(lineNumber);
				disable();
				return -1;
			}
			labels[label & 1023] = lines.size();
			if (labelEnd == std::string::npos)
				continue;
			sourceLine = trim(sourceLine.substr(labelEnd));
			if (sourceLine.empty())
				continue;
		}

		std::istringstream tokens(sourceLine);
		ParsedLine parsed;
		parsed.number = lineNumber;
		tokens >> parsed.command;
		parsed.command = lower(parsed.command);
		std::string argument;
		while (tokens >> argument)
			parsed.arguments.push_back(lower(argument));
		lines.push_back(parsed);
	}

	auto parseOperand = [](const std::string& token, bool destination, bool allowImmediate,
		Operand& operand) -> bool
	{
		std::string value = lower(token);
		std::uint32_t index = 0;

		if (value[0] == '#')
		{
			if (value == "#?")
			{
				if (!allowImmediate)
					return false;
				operand = Operand(OperandType::Immediate, 0, destination ? 1 : 0);
				return true;
			}
			if (!allowImmediate || !parseNumber(value.substr(1), index))
				return false;
			operand = { OperandType::Immediate, index };
			return true;
		}
		if (value[0] == 'w' && parseNumber(value.substr(1), index))
		{
			operand = { OperandType::Work, index & 7 };
			return true;
		}
		if (value[0] == 'i' && parseNumber(value.substr(1), index))
		{
			operand = { OperandType::InputPort, index & 3 };
			return true;
		}
		if (value[0] == 'o' && parseNumber(value.substr(1), index))
		{
			operand = { OperandType::OutputPort, index & 3 };
			return true;
		}
		auto parseMemory = [&](const char* prefix, OperandType type) -> bool
		{
			const std::size_t prefixLength = std::strlen(prefix);
			if (value.size() <= prefixLength ||
				value.compare(0, prefixLength, prefix) != 0)
				return false;
			const std::string address = value.substr(prefixLength);
			if (address == "?")
			{
				operand = Operand(type, 0, destination ? 1 : 0);
				return true;
			}
			if (!parseNumber(address, index))
				return false;
			operand = Operand(type, index);
			return true;
		};
		if (parseMemory("dd", OperandType::Data32) ||
			parseMemory("dw", OperandType::Data16) ||
			parseMemory("db", OperandType::Data8) ||
			parseMemory("d", OperandType::Data8))
			return !destination || allowImmediate;
		if (parseMemory("rd", OperandType::Ram32) ||
			parseMemory("rw", OperandType::Ram16) ||
			parseMemory("rb", OperandType::Ram8) ||
			parseMemory("r", OperandType::Ram8))
			return true;
		if (parseNumber(value, index))
		{
			operand = { destination ? OperandType::InputPort : OperandType::OutputPort, index & 3 };
			return true;
		}
		return false;
	};

	std::vector<Instruction> compiled;
	for (const ParsedLine& line : lines)
	{
		Instruction instruction = {};
		instruction.first = { OperandType::Immediate, 0 };
		instruction.second = { OperandType::Immediate, 0 };
		instruction.target = 0;

		const std::string& command = line.command;
		if (command == "q")
			instruction.code = OpCode::Quit;
		else if (command == "nop")
			instruction.code = OpCode::Nop;
		else if (command == "w" && line.arguments.size() == 1)
		{
			instruction.code = OpCode::Wait;
			std::uint32_t wait = 0;
			if (parseNumber(line.arguments[0], wait))
				instruction.first = { OperandType::Immediate, wait };
			else if (!parseOperand(line.arguments[0], false, true, instruction.first))
				goto compile_error;
		}
		else if ((command == "m" || command == "c" || command == "a" || command == "s" ||
				  command == "u" || command == "d") && line.arguments.size() == 2)
		{
			const bool compare = command == "c";
			if (!parseOperand(line.arguments[0], false, true, instruction.first) ||
				!parseOperand(line.arguments[1], true, compare, instruction.second))
				goto compile_error;
			if (command == "m") instruction.code = OpCode::Move;
			if (command == "c") instruction.code = OpCode::Compare;
			if (command == "a") instruction.code = OpCode::Add;
			if (command == "s") instruction.code = OpCode::Subtract;
			if (command == "u") instruction.code = OpCode::Multiply;
			if (command == "d") instruction.code = OpCode::Divide;
		}
		else if (command == "n" && line.arguments.size() == 3)
		{
			if (!parseOperand(line.arguments[0], false, true, instruction.first) ||
				!parseOperand(line.arguments[2], true, line.arguments[1] == "!",
					instruction.second))
				goto compile_error;
			const std::unordered_map<std::string, OpCode> operations = {
				{ "+", OpCode::Add }, { "-", OpCode::Subtract },
				{ "*", OpCode::Multiply }, { "/", OpCode::Divide },
				{ "\\", OpCode::Divide }, { "%", OpCode::ModuloSigned },
				{ "$", OpCode::ModuloUnsigned }, { "&", OpCode::And },
				{ "|", OpCode::Or }, { "^", OpCode::Xor },
				{ "<", OpCode::ShiftLeft }, { "_", OpCode::ShiftRightSigned },
				{ ">", OpCode::ShiftRightUnsigned }, { "!", OpCode::Not }
			};
			const auto operation = operations.find(line.arguments[1]);
			if (operation == operations.end())
				goto compile_error;
			instruction.code = operation->second;
		}
		else
		{
			const std::unordered_map<std::string, OpCode> branches = {
				{ "bra", OpCode::Branch }, { "beq", OpCode::BranchEqual },
				{ "bne", OpCode::BranchNotEqual }, { "bge", OpCode::BranchGreaterEqual },
				{ "ble", OpCode::BranchLessEqual }, { "bgt", OpCode::BranchGreater },
				{ "blt", OpCode::BranchLess }, { "bcc", OpCode::BranchCarryClear },
				{ "blo", OpCode::BranchLower }, { "bhi", OpCode::BranchHigher },
				{ "bcs", OpCode::BranchCarrySet }
			};
			const auto branch = branches.find(command);
			std::uint32_t label = 0;
			if (branch == branches.end() || line.arguments.size() != 1 ||
				!parseNumber(line.arguments[0], label) || labels.find(label & 1023) == labels.end())
				goto compile_error;
			instruction.code = branch->second;
			instruction.target = labels[label & 1023];
		}
		compiled.push_back(instruction);
		continue;

compile_error:
		error_ = "Unsupported or invalid command at line " + std::to_string(line.number) +
			": " + line.command;
		for (const std::string& argument : line.arguments)
			error_ += " " + argument;
		disable();
		return -1;
	}

	program_.swap(compiled);
	data_.swap(parsedData);
	volume_ = parsedVolume;
	std::copy(parsedSourceMute, parsedSourceMute + 256, sourceMute_);
	error_.clear();
	enabled_ = !program_.empty() || !data_.empty() || volume_ != 1.0f ||
		std::find(sourceMute_, sourceMute_ + 256, true) != sourceMute_ + 256;
	reset();
	return enabled_ ? static_cast<int>(std::max<std::size_t>(program_.size(), 1)) : -1;
}

std::uint32_t Script700::read(const Operand& operand) const
{
	const std::uint32_t index = operand.compareIndex >= 0 ?
		compare_[operand.compareIndex] : operand.index;
	switch (operand.type)
	{
	case OperandType::Immediate: return index;
	case OperandType::InputPort: return inputPorts_ ? inputPorts_[index & 3] : 0;
	case OperandType::OutputPort: return outputPorts_ ? outputPorts_[index & 3] : 0;
	case OperandType::Work: return work_[index & 7];
	case OperandType::Ram8: return ram_ ? ram_[index & 0xFFFF] : 0;
	case OperandType::Ram16:
		return ram_ ? ram_[index & 0xFFFF] |
			(static_cast<std::uint32_t>(ram_[(index + 1) & 0xFFFF]) << 8) : 0;
	case OperandType::Ram32:
		return ram_ ? ram_[index & 0xFFFF] |
			(static_cast<std::uint32_t>(ram_[(index + 1) & 0xFFFF]) << 8) |
			(static_cast<std::uint32_t>(ram_[(index + 2) & 0xFFFF]) << 16) |
			(static_cast<std::uint32_t>(ram_[(index + 3) & 0xFFFF]) << 24) : 0;
	case OperandType::Data8:
	case OperandType::Data16:
	case OperandType::Data32:
		{
			const unsigned count = operand.type == OperandType::Data32 ? 4 :
				(operand.type == OperandType::Data16 ? 2 : 1);
			std::uint32_t result = 0;
			for (unsigned i = 0; i < count; ++i)
				if (static_cast<std::size_t>(index) + i < data_.size())
					result |= static_cast<std::uint32_t>(data_[index + i]) << (i * 8);
			return result;
		}
	}
	return 0;
}

void Script700::write(const Operand& operand, std::uint32_t value)
{
	const std::uint32_t index = operand.compareIndex >= 0 ?
		compare_[operand.compareIndex] : operand.index;
	switch (operand.type)
	{
	case OperandType::InputPort:
		if (inputPorts_)
		{
			const std::uint8_t port = static_cast<std::uint8_t>(index & 3);
			const std::uint8_t byte = static_cast<std::uint8_t>(value);
			inputPorts_[port] = byte;
			inputPortWrites_.push_back(std::make_pair(port, byte));
		}
		break;
	case OperandType::OutputPort:
		if (outputPorts_) outputPorts_[index & 3] = static_cast<std::uint8_t>(value);
		break;
	case OperandType::Work:
		work_[index & 7] = value;
		break;
	case OperandType::Ram8:
		if (ram_) ram_[index & 0xFFFF] = static_cast<std::uint8_t>(value);
		break;
	case OperandType::Ram16:
		if (ram_)
		{
			ram_[index & 0xFFFF] = static_cast<std::uint8_t>(value);
			ram_[(index + 1) & 0xFFFF] = static_cast<std::uint8_t>(value >> 8);
		}
		break;
	case OperandType::Ram32:
		if (ram_)
			for (unsigned i = 0; i < 4; ++i)
				ram_[(index + i) & 0xFFFF] = static_cast<std::uint8_t>(value >> (i * 8));
		break;
	case OperandType::Data8:
	case OperandType::Data16:
	case OperandType::Data32:
	case OperandType::Immediate:
		break;
	}
}

void Script700::advance(std::uint64_t ticks)
{
	if (!enabled_ || stopped_)
		return;
	if (waitTicks_ > ticks)
	{
		waitTicks_ -= ticks;
		return;
	}
	ticks -= waitTicks_;
	waitTicks_ = 0;
	run();
	while (!stopped_ && waitTicks_ && ticks >= waitTicks_)
	{
		ticks -= waitTicks_;
		waitTicks_ = 0;
		run();
	}
	if (!stopped_ && waitTicks_)
		waitTicks_ -= ticks;
}

void Script700::run()
{
	std::size_t budget = 100000;
	while (!stopped_ && waitTicks_ == 0 && pc_ < program_.size() && budget--)
	{
		const Instruction& instruction = program_[pc_++];
		const std::uint32_t source = read(instruction.first);
		std::uint32_t destination = read(instruction.second);
		bool branch = false;

		switch (instruction.code)
		{
		case OpCode::Wait:
			waitTicks_ = source;
			break;
		case OpCode::Move: write(instruction.second, source); break;
		case OpCode::Compare: compare_[0] = source; compare_[1] = destination; break;
		case OpCode::Add: write(instruction.second, destination + source); break;
		case OpCode::Subtract: write(instruction.second, destination - source); break;
		case OpCode::Multiply: write(instruction.second, destination * source); break;
		case OpCode::Divide: write(instruction.second, source ? destination / source : 0); break;
		case OpCode::ModuloSigned:
			write(instruction.second, source ? static_cast<std::uint32_t>(
				static_cast<std::int32_t>(destination) % static_cast<std::int32_t>(source)) : 0);
			break;
		case OpCode::ModuloUnsigned:
			write(instruction.second, source ? destination % source : 0);
			break;
		case OpCode::And: write(instruction.second, destination & source); break;
		case OpCode::Or: write(instruction.second, destination | source); break;
		case OpCode::Xor: write(instruction.second, destination ^ source); break;
		case OpCode::ShiftLeft: write(instruction.second, destination << (source & 31)); break;
		case OpCode::ShiftRightSigned:
			write(instruction.second, static_cast<std::uint32_t>(
				static_cast<std::int32_t>(destination) >> (source & 31)));
			break;
		case OpCode::ShiftRightUnsigned:
			write(instruction.second, destination >> (source & 31));
			break;
		case OpCode::Not: write(instruction.second, ~source); break;
		case OpCode::Branch: branch = true; break;
		case OpCode::BranchEqual: branch = compare_[0] == compare_[1]; break;
		case OpCode::BranchNotEqual: branch = compare_[0] != compare_[1]; break;
		case OpCode::BranchGreaterEqual:
			branch = static_cast<std::int32_t>(compare_[0]) <= static_cast<std::int32_t>(compare_[1]); break;
		case OpCode::BranchLessEqual:
			branch = static_cast<std::int32_t>(compare_[0]) >= static_cast<std::int32_t>(compare_[1]); break;
		case OpCode::BranchGreater:
			branch = static_cast<std::int32_t>(compare_[0]) < static_cast<std::int32_t>(compare_[1]); break;
		case OpCode::BranchLess:
			branch = static_cast<std::int32_t>(compare_[0]) > static_cast<std::int32_t>(compare_[1]); break;
		case OpCode::BranchCarryClear:
			branch = compare_[0] <= compare_[1]; break;
		case OpCode::BranchLower: branch = compare_[0] >= compare_[1]; break;
		case OpCode::BranchHigher: branch = compare_[0] < compare_[1]; break;
		case OpCode::BranchCarrySet: branch = compare_[0] > compare_[1]; break;
		case OpCode::Quit: stopped_ = true; break;
		case OpCode::Nop: break;
		}
		if (branch)
			pc_ = instruction.target;
	}
	if (pc_ >= program_.size() || budget == 0)
		stopped_ = true;
}
