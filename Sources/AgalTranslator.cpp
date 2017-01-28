#include "AgalTranslator.h"
#include <SPIRV/spirv.hpp>
#include "../glslang/glslang/Public/ShaderLang.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <string.h>
#include <sstream>

using namespace krafix;

namespace {
	typedef unsigned id;

	struct ConstantVariable {
		unsigned id;
		unsigned type;
		int size;
		bool hardCoded = true;
		std::vector<std::string> operands;

		ConstantVariable() {}
	};

	struct Variable {
		unsigned id;
		unsigned type;
		spv::StorageClass storage;
		bool builtin;

		Variable() : builtin(false) {}
	};

	struct Type {
		const char* name;
		unsigned length;
		bool isarray;

		Type() : name("unknown"), length(1), isarray(false) {}
	};

	std::map<unsigned, Variable> variables;
	std::map<unsigned, Type> types;
	std::vector<ConstantVariable> constants;

	enum Opcode {
		con, // pseudo instruction for constants
		add,
		sub,
		mul,
		div,
		mov,
		m44,
		tex,
		cos,
		sin,
		nrm,
		min,
		max,
		unknown
	};

	enum RegisterType {
		Attribute,
		Constant,
		Temporary,
		Output,
		Varying,
		Sampler,
		Unused
	};

	struct Register {
		RegisterType type;
		int number;
		int size;
		std::string swizzle;
		unsigned spirIndex;

		Register() : type(Unused), number(-1), swizzle("xyzw"), size(1), spirIndex(0) { }

		Register(ShaderStage stage, unsigned spirIndex, const std::string& swizzle = "xyzw", int size = 1) : number(-1), swizzle(swizzle), size(size), spirIndex(spirIndex) {
			bool isConstant = false;
			int constantID = 0;

			int offset = 0;
			for (unsigned i = 0; i < constants.size(); i++)
			{
				if (constants[i].id == spirIndex)
				{
					constantID = offset;
					isConstant = true;
					break;
				}
				offset += constants[i].size;
			}

			if (isConstant)
			{
				number = constantID;
				type = Constant;
				return;
			}

			if (variables.find(spirIndex) == variables.end()) {
				type = Temporary;
			}
			else {
				Variable variable = variables[spirIndex];
				switch (variable.storage) {
				case spv::StorageClassUniformConstant: {
					Type t = types[variable.type];
					if (strcmp(t.name, "sampler2D") == 0) {
						type = Sampler;
					}
					else {
						type = Constant;

						ConstantVariable variable;
						variable.id = spirIndex;
						variable.type = type;
						variable.size = size;
						variable.hardCoded = false;
						variable.operands.push_back("0.0");
						constants.push_back(variable);
					}
					break;
				}
				case spv::StorageClassInput:
					if (stage == StageVertex) type = Attribute;
					else type = Varying;
					break;
				case spv::StorageClassUniform:
					type = Constant;
					break;
				case spv::StorageClassOutput:
					if (stage == StageVertex) type = Varying;
					else type = Output;
					break;
				case spv::StorageClassFunction:
					type = Temporary;
					break;
				}
			}
		}
	};

	struct Agal {
		Opcode opcode;
		Register destination;
		Register source1;
		Register source2;

		Agal(Opcode opcode, Register destination, Register source1) : opcode(opcode), destination(destination), source1(source1) { }

		Agal(Opcode opcode, Register destination, Register source1, Register source2) : opcode(opcode), destination(destination), source1(source1), source2(source2) { }
	};

	struct Name {
		const char* name;
	};

	const char* indexName(unsigned index) {
		switch (index) {
		case 0:
			return "x";
		case 1:
			return "y";
		case 2:
			return "z";
		case 3:
		default:
			return "w";
		}
	}

	const char* indexName4(unsigned index) {
		switch (index) {
		case 0:
			return "xxxx";
		case 1:
			return "yyyy";
		case 2:
			return "zzzz";
		case 3:
		default:
			return "wwww";
		}
	}

	void assignRegisterNumber(Register& reg, std::map<unsigned, Register>& assigned, int& nextTemporary, int& nextAttribute, int& nextConstant, int& nextSampler) {
		if (reg.type == Unused) return;

		if (reg.spirIndex != 0 && assigned.find(reg.spirIndex) != assigned.end()) {
			reg.number = assigned[reg.spirIndex].number;
			reg.type = assigned[reg.spirIndex].type;
		}
		else {
			switch (reg.type) {
			case Temporary:
				reg.number = nextTemporary;
				if (reg.spirIndex != 0) assigned[reg.spirIndex] = reg;
				nextTemporary += reg.size;
				break;
			case Attribute:
				reg.number = nextAttribute;
				if (reg.spirIndex != 0) assigned[reg.spirIndex] = reg;
				nextAttribute += reg.size;
				break;
			case Varying:
				break;
			case Constant:
				reg.number = nextConstant;
				if (reg.spirIndex != 0) assigned[reg.spirIndex] = reg;
				nextConstant += reg.size;
				break;
			case Sampler:
				break;
			}
		}
	}
	void addNameTo(std::string name, std::vector<std::string>& names) {
		for (auto s : names) {
			if (s == name) return;
		}
		names.push_back(name);
	}

	bool reMapInstruction(Agal instruction, RegisterType type, std::map<std::string, int>& newNumbers, std::map<unsigned, Register>& assigned, std::map<unsigned, Name>& names) {
		if (instruction.destination.type == type) {
			std::string name = names[instruction.destination.spirIndex].name;
			instruction.destination.number = newNumbers[name];
			assigned[instruction.destination.spirIndex] = instruction.destination;
			return true;
		}
		else if (instruction.source1.type == type) {
			std::string name = names[instruction.source1.spirIndex].name;
			instruction.source1.number = newNumbers[name];
			assigned[instruction.source1.spirIndex] = instruction.source1;
			return true;
		}
		else if (instruction.source2.type == type) {
			std::string name = names[instruction.source2.spirIndex].name;
			instruction.source2.number = newNumbers[name];
			assigned[instruction.source2.spirIndex] = instruction.source2;
			return true;
		}
		return false;
	}

	const char* getName(std::map<unsigned, Name>& names, unsigned index) {
		if (names[index].name == nullptr) {
			char* buffer = new char[7];
			buffer[0] = '_';
			buffer[1] = 0;
			sprintf(buffer + 1, "%i", index);
			names[index].name = buffer;
		}
		return names[index].name;
	}

	void assignRegisterNumbers(std::vector<Agal>& agal, std::map<unsigned, Register>& assigned, std::map<unsigned, Name>& names) {
		int nextTemporary = 0;
		int nextAttribute = 0;
		int nextConstant = 0;
		int nextSampler = 0;

		std::vector<std::string> varyings;
		std::vector<std::string> samplers;
		for (unsigned i = 0; i < agal.size(); ++i) {
			Agal& instruction = agal[i];
			if (instruction.destination.type == Varying) {
				addNameTo(getName(names, instruction.destination.spirIndex), varyings);
			}
			else if (instruction.source1.type == Varying) {
				addNameTo(getName(names, instruction.source1.spirIndex), varyings);
			}
			else if (instruction.source2.type == Varying) {
				addNameTo(getName(names, instruction.source2.spirIndex), varyings);
			}
			else if (instruction.destination.type == Sampler) {
				addNameTo(getName(names, instruction.destination.spirIndex), samplers);
			}
			else if (instruction.source1.type == Sampler) {
				addNameTo(getName(names, instruction.source1.spirIndex), samplers);
			}
			else if (instruction.source2.type == Sampler) {
				addNameTo(getName(names, instruction.source2.spirIndex), samplers);
			}
		}
		std::sort(varyings.begin(), varyings.end());
		std::sort(samplers.begin(), samplers.end());
		std::map<std::string, int> varyingNumbers;
		std::map<std::string, int> samplerNumbers;
		for (unsigned i = 0; i < varyings.size(); ++i) {
			varyingNumbers[varyings[i]] = i;
		}
		for (unsigned i = 0; i < samplers.size(); ++i) {
			samplerNumbers[samplers[i]] = i;
		}

		for (unsigned i = 0; i < agal.size(); ++i) {
			Agal& instruction = agal[i];
			if (instruction.opcode == unknown) continue;
			if (!reMapInstruction(instruction, Varying, varyingNumbers, assigned, names))
			{
				reMapInstruction(instruction, Sampler, samplerNumbers, assigned, names);
			}
		}

		for (unsigned i = 0; i < agal.size(); ++i) {
			Agal& instruction = agal[i];
			assignRegisterNumber(instruction.destination, assigned, nextTemporary, nextAttribute, nextConstant, nextSampler);
			assignRegisterNumber(instruction.source1, assigned, nextTemporary, nextAttribute, nextConstant, nextSampler);
			assignRegisterNumber(instruction.source2, assigned, nextTemporary, nextAttribute, nextConstant, nextSampler);
		}
	}

	void outputRegister(std::ostream& out, Register reg, ShaderStage stage, int registerOffset) {
		switch (reg.type) {
		case Attribute:
			out << "va";
			break;
		case Constant:
			if (stage == StageVertex) out << "vc";
			else out << "fc";
			break;
		case Temporary:
			if (stage == StageVertex) out << "vt";
			else out << "ft";
			break;
		case Output:
			if (stage == StageVertex) out << "op";
			else out << "oc";
			break;
		case Varying:
			out << "v";
			break;
		case Sampler:
			out << "fs";
			break;
		case Unused:
			out << "unused";
			break;
		}
		if (reg.type != Output) {
			out << reg.number + registerOffset;
		}
		if (reg.swizzle != "xyzw") {
			out << "." << reg.swizzle;
		}
	}

	ConstantVariable findConstant(unsigned id) {
		for (size_t i = 0; i < constants.size(); ++i) {
			if (constants[i].id == id) {
				return constants[i];
			}
		}
		ConstantVariable invalid;
		invalid.id = 0;
		invalid.size = 0;
		return invalid;
	}
}

void AgalTranslator::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, std::string> tmp_constants;
	constants.clear();
	types.clear();
	variables.clear();
	unsigned vertexOutput = 0;

	std::vector<Agal> agal;

	if (stage == StageVertex) {
		//clip space constant
		Register reg(stage, 99999);
		reg.type = Constant;
		reg.size = 1;
		agal.push_back(Agal(con, reg, Register()));

		tmp_constants[99999] = "0.5";

		ConstantVariable variable;
		variable.id = 99999;
		variable.type = 0;
		variable.size = 1;
		variable.operands.push_back("0.5");

		constants.insert(constants.begin(), variable);
	}

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		case OpName: {
			unsigned id = inst.operands[0];
			if (strcmp(inst.string, "") != 0) {
				Name n;
				n.name = inst.string;
				names[id] = n;
			}
			break;
		}
		case OpTypePointer: {
			Type t;
			unsigned id = inst.operands[0];
			Type subtype = types[inst.operands[2]];
			t.name = subtype.name;
			t.isarray = subtype.isarray;
			t.length = subtype.length;
			types[id] = t;
			break;
		}
		case OpTypeFloat: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "float";
			types[id] = t;
			break;
		}
		case OpTypeInt: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "int";
			types[id] = t;
			break;
		}
		case OpTypeBool: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "bool";
			types[id] = t;
			break;
		}
		case OpTypeStruct: {
			Type t;
			unsigned id = inst.operands[0];
			// TODO: members
			Name n = names[id];
			t.name = n.name;
			types[id] = t;
			break;
		}
		case OpConstant: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;

			std::string value = "unknown";
			if (strcmp(resultType.name, "float") == 0) {
				float f = *(float*)&inst.operands[2];
				std::stringstream strvalue;
				strvalue << f;
				if (strvalue.str().find('.') == std::string::npos) strvalue << ".0";
				value = strvalue.str();
			}
			if (strcmp(resultType.name, "int") == 0) {
				std::stringstream strvalue;
				strvalue << *(int*)&inst.operands[2];
				value = strvalue.str();
			}

			tmp_constants[result] = value;

			Register reg(stage, result);
			reg.type = Constant;
			reg.size = 1;
			agal.push_back(Agal(con, reg, Register()));

			//todo: clean out the unused constants at the end (for example, because they are used in a composite constant).
			ConstantVariable variable;
			variable.id = inst.operands[1];
			variable.size = 1;
			variable.type = inst.operands[0];
			variable.operands.push_back(value);
			variable.operands.push_back(value);
			variable.operands.push_back(value);
			variable.operands.push_back(value);
			constants.insert(constants.begin(),variable);

			break;
		}
		case OpConstantComposite: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;

			ConstantVariable variable;
			variable.id = inst.operands[1];
			variable.type = inst.operands[0];
			variable.size = 1;

			for (unsigned i = 2; i < inst.length; i++)
			{
				variable.operands.push_back(tmp_constants[inst.operands[i]]);
			}

			constants.insert(constants.begin(),variable);
			//result = vec4(inst.operands[2], inst.operands[3], inst.operands[4], inst.operands[5])
			break;
		}
		case OpTypeArray: {
			Type t;
			t.name = "unknownarray";
			t.isarray = true;
			unsigned id = inst.operands[0];
			Type subtype = types[inst.operands[1]];
			//t.length = atoi(references[inst.operands[2]].c_str());
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0) {
					t.name = "float";
				}
				else if (strcmp(subtype.name, "vec2") == 0) {
					t.name = "vec2";
				}
				else if (strcmp(subtype.name, "vec3") == 0) {
					t.name = "vec3";
				}
				else if (strcmp(subtype.name, "vec4") == 0) {
					t.name = "vec4";
				}
			}
			types[id] = t;
			break;
		}
		case OpTypeVector: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "vec?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 2) {
					t.name = "vec2";
					t.length = 2;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
					t.name = "vec3";
					t.length = 3;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
					t.name = "vec4";
					t.length = 4;
				}
			}
			types[id] = t;
			break;
		}
		case OpTypeMatrix: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "mat?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "vec3") == 0 && inst.operands[2] == 3) {
					t.name = "mat3";
					t.length = 9;
					types[id] = t;
				}
				else if (strcmp(subtype.name, "vec4") == 0 && inst.operands[2] == 4) {
					t.name = "mat4";
					t.length = 16;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeImage: {
			Type t;
			unsigned id = inst.operands[0];
			bool video = inst.length >= 8 && inst.operands[8] == 1;
			t.name = "sampler2D";
			types[id] = t;
			break;
		}
		case OpTypeSampler: {
			break;
		}
		case OpTypeSampledImage: {
			Type t;
			unsigned id = inst.operands[0];
			unsigned image = inst.operands[1];
			types[id] = types[image];
			break;
		}
		case OpVariable: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;
			Variable& v = variables[result];
			v.id = result;
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];
			break;
		}
		case OpFunction:
			break;
		case OpFunctionEnd:
			break;
		case OpCompositeConstruct: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			agal.push_back(Agal(mov, Register(stage, result, "x"), Register(stage, inst.operands[2], "x")));
			agal.push_back(Agal(mov, Register(stage, result, "y"), Register(stage, inst.operands[3], "y")));
			agal.push_back(Agal(mov, Register(stage, result, "z"), Register(stage, inst.operands[4], "z")));
			agal.push_back(Agal(mov, Register(stage, result, "w"), Register(stage, inst.operands[5], "w")));
			break;
		}
		case OpCompositeExtract: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned composite = inst.operands[2];
			
			agal.push_back(Agal(mov, Register(stage, result, "xyzw"), Register(stage, composite, indexName4(inst.operands[3]))));
			break;
		}
		case OpMatrixTimesVector: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned matrix = inst.operands[2];
			unsigned vector = inst.operands[3];
			agal.push_back(Agal(m44, Register(stage, result), Register(stage, vector), Register(stage, matrix, "xyzw", 4)));
			break;
		}
		case OpImageSampleImplicitLod: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned sampler = inst.operands[2];
			unsigned coordinate = inst.operands[3];
			Register samplerReg(stage, sampler);
			samplerReg.type = Sampler;
			agal.push_back(Agal(tex, Register(stage, result), Register(stage, coordinate), samplerReg));
			break;
		}
		case OpVectorShuffle: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector1 = inst.operands[2];
			auto t1 = types[inst.operands[2]];
			unsigned vector1length = types[inst.operands[2]].length;
			unsigned vector2 = inst.operands[3];
			auto t2 = types[inst.operands[3]];
			unsigned vector2length = types[inst.operands[3]].length;

			std::string r1swizzle;
			std::string r2swizzle;
			std::string v1swizzle;
			std::string v2swizzle;
			for (unsigned i = 4; i < inst.length; ++i) {
				unsigned index = inst.operands[i];
				if (index < vector1length) {
					r1swizzle += indexName(i - 4);
					v1swizzle += indexName(index);
				}
				else {
					r2swizzle += indexName(i - 4);
					v2swizzle += indexName(index - vector1length);
				}
			}
			agal.push_back(Agal(mov, Register(stage, result), Register(stage, vector1)));
			agal.push_back(Agal(mov, Register(stage, result, r1swizzle), Register(stage, vector1, v1swizzle)));
			if (r2swizzle.size() > 0) agal.push_back(Agal(mov, Register(stage, result, r2swizzle), Register(stage, vector2, v2swizzle)));
			break;
		}
		case OpFMul: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			agal.push_back(Agal(mul, Register(stage, result), Register(stage, operand1), Register(stage, operand2)));
			break;
		}
		case OpFAdd: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			agal.push_back(Agal(add, Register(stage, result), Register(stage, operand1), Register(stage, operand2)));
			break;
		}
		case OpFSub: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			agal.push_back(Agal(sub, Register(stage, result), Register(stage, operand1), Register(stage, operand2)));
			break;
		}
		case OpFDiv: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			agal.push_back(Agal(Opcode::div, Register(stage, result), Register(stage, operand1), Register(stage, operand2)));
			break;
		}

		case OpVectorTimesScalar: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector = inst.operands[2];
			unsigned scalar = inst.operands[3];
			agal.push_back(Agal(mul, Register(stage, result), Register(stage, vector), Register(stage, scalar)));
			break;
		}
		case OpExecutionMode:
			break;
		case OpReturn:
			break;
		case OpLabel:
			break;
		case OpBranch:
			break;
		case OpDecorate: {
			unsigned target = inst.operands[0];
			Decoration decoration = (Decoration)inst.operands[1];
			if (decoration == DecorationBuiltIn) {
				variables[target].builtin = true;
			}
			break;
		}
		case OpTypeFunction: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "function";
			types[id] = t;
			break;
		}
		case OpTypeVoid:
			break;
		case OpEntryPoint:
			break;
		case OpMemoryModel:
			break;
		case OpExtInstImport:
			break;
		case OpSource:
			break;
		case OpCapability:
			break;
		case OpLoad: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;

			Register r1(stage, result,"xyzw",(resultType.length + 3) / 4);
			Register r2(stage, inst.operands[2],"xyzw",(types[inst.operands[2]].length + 3) / 4);

			if (strcmp(types[inst.operands[2]].name, "sampler2D") == 0) {
				names[result] = names[inst.operands[2]];
			}
			else {
				agal.push_back(Agal(mov, r1, r2));
			}
			break;
		}
		case OpStore: {
			Variable v = variables[inst.operands[0]];
			if (v.builtin && stage == StageFragment) {
				Register oc(stage, inst.operands[0]);
				oc.type = Output;
				oc.number = 0;
				agal.push_back(Agal(mov, oc, Register(stage, inst.operands[1])));
			}
			else if (v.builtin && stage == StageVertex) {
				vertexOutput = inst.operands[0];
				Register tempop(stage, inst.operands[0]);
				tempop.type = Temporary;
				agal.push_back(Agal(mov, tempop, Register(stage, inst.operands[1])));
			}
			else {
				Type t1 = types[inst.operands[0]];
				Type t2 = types[inst.operands[1]];
				Register r1(stage, inst.operands[0]);
				if (strcmp(t1.name, "mat4") == 0) {
					r1.size = 4;
				}
				Register r2(stage, inst.operands[1]);
				if (strcmp(t2.name, "mat4") == 0) {
					r2.size = 4;
				}
				agal.push_back(Agal(mov, r1, r2));
			}
			break;
		}
		case OpExtInst: {
			Type& resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;
			id set = inst.operands[2];
			{
				GLSLstd450 instruction = (GLSLstd450)inst.operands[3];
				switch (instruction)
				{
				case GLSLstd450Cos:
					agal.push_back(Agal(Opcode::cos, Register(stage, inst.operands[1]), Register(stage, inst.operands[4])));
					break;
				case GLSLstd450Sin:
					agal.push_back(Agal(Opcode::sin, Register(stage, inst.operands[1]), Register(stage, inst.operands[4])));
					break;
				case GLSLstd450Normalize:
					agal.push_back(Agal(nrm, Register(stage, inst.operands[1], "xyz"), Register(stage, inst.operands[3])));
					break;
				case GLSLstd450FMin:
					agal.push_back(Agal(Opcode::min, Register(stage, inst.operands[1]), Register(stage, inst.operands[3]), Register(stage, inst.operands[4])));
					break;
				case GLSLstd450FMax:
					agal.push_back(Agal(Opcode::max, Register(stage, inst.operands[1]), Register(stage, inst.operands[3]), Register(stage, inst.operands[4])));
					break;
				default:
					printf("Unknown extinst '%i' in the agal translator.\n", instruction);
				}
			}
			break;
		}
		case OpAccessChain: {
			std::stringstream swizzle;
			for (unsigned i = 3; i < inst.length; ++i) {
				ConstantVariable constvar = findConstant(inst.operands[i]);
				swizzle << indexName(atoi(constvar.operands[0].c_str()));
			}
			agal.push_back(Agal(mov, Register(stage, inst.operands[1]), Register(stage, inst.operands[2], swizzle.str())));
			break;
		}
		default:
			//Agal instruction(unknown, Register(stage, inst.opcode), Register(stage, inst.opcode));
			//instruction.destination.number = inst.opcode;
			//agal.push_back(instruction);
			break;
		}
	}

	//adjust clip space
	if (stage == StageVertex) {
		Register poszzzz(stage, vertexOutput, "zzzz");
		poszzzz.type = Temporary;
		Register poswwww(stage, vertexOutput, "wwww");
		poswwww.type = Temporary;
		agal.push_back(Agal(add, Register(stage, 99998, "xxxx"), poszzzz, poswwww));
		Register posz(stage, vertexOutput, "z");
		posz.type = Temporary;
		Register reg(stage, 99999);
		reg.type = Constant;
		reg.swizzle = "x";
		agal.push_back(Agal(mul, posz, reg, Register(stage, 99998, "x")));

		Register op(stage, 0);
		op.type = Output;
		op.number = 0;
		Register pos(stage, vertexOutput);
		pos.type = Temporary;
		agal.push_back(Agal(mov, op, pos));
	}

	std::map<unsigned, Register> assigned;
	for (unsigned i = 0; i < constants.size(); ++i) {
		assigned[constants[i].id] = Register(stage, constants[i].id);
	}
	assignRegisterNumbers(agal, assigned, names);

	//Optimize, todo: optimize this optimize pass
	std::map<int, int> firstUsed;
	std::map<int, int> lastUsed;
	for (int i = agal.size() - 1; i >= 0; i--)
	{
		Agal& instruction = agal[i];
		if (instruction.opcode == unknown) {
			continue;
		}

		if (instruction.source1.type == Temporary)
		{
			firstUsed[instruction.source1.spirIndex] = i;
			if (lastUsed.find(instruction.source1.spirIndex) == lastUsed.end())
			{
				lastUsed[instruction.source1.spirIndex] = i;
			}
		}

		if (instruction.source2.type == Temporary)
		{
			firstUsed[instruction.source2.spirIndex] = i;
			if (lastUsed.find(instruction.source2.spirIndex) == lastUsed.end())
			{
				lastUsed[instruction.source2.spirIndex] = i;
			}
		}

		if (instruction.destination.type == Temporary)
		{
			firstUsed[instruction.destination.spirIndex] = i;
			if (lastUsed.find(instruction.destination.spirIndex) == lastUsed.end())
			{
				lastUsed[instruction.destination.spirIndex] = i;
			}
		}
	}

	std::map<int, int> currentlyUsed;
	std::vector<bool> tempRegisters;
	for (unsigned i = 0; i < 26; i++)
	{
		tempRegisters.push_back(false);
	}

	auto free_register = [&](int size = 1) {
		for (int i = 0; i < 26; i++)
		{
			bool found = true;
			for (int j = 0; j < size; j++)
			{
				if (tempRegisters[i + j])
				{
					found = false;
					break;
				}
			}

			if (found)
				return i;
		}

		return -1;
	};

	for (unsigned i = 0; i < agal.size(); ++i) {
		Agal& instruction = agal[i];

		if (instruction.opcode == unknown) {
			continue;
		}

		if (instruction.source1.type == Temporary) {
			instruction.source1.number = currentlyUsed[instruction.source1.spirIndex];
			if (i == lastUsed[instruction.source1.spirIndex]) {
				for (int j = 0; j < instruction.source1.size; j++)
					tempRegisters[currentlyUsed[instruction.source1.spirIndex] + j] = false;
			}
		}

		if (instruction.source2.type == Temporary) {
			instruction.source2.number = currentlyUsed[instruction.source2.spirIndex];
			if (i == lastUsed[instruction.source2.spirIndex]) {
				for (int j = 0; j < instruction.source2.size; j++)
					tempRegisters[currentlyUsed[instruction.source2.spirIndex] + j] = false;
			}
		}

		if (instruction.destination.type == Temporary) {
			if (currentlyUsed.find(instruction.destination.spirIndex) == currentlyUsed.end()) {
				int tmpRegister = free_register(instruction.destination.size);
				//todo: handle out of temporaries

				currentlyUsed[instruction.destination.spirIndex] = tmpRegister;

				for (int j = 0; j < instruction.destination.size; ++j)
					tempRegisters[tmpRegister + j] = true;
			}

			instruction.destination.number = currentlyUsed[instruction.destination.spirIndex];
			if (i == lastUsed[instruction.destination.spirIndex]) {
				for (int j = 0; j < instruction.destination.size; ++j)
					tempRegisters[currentlyUsed[instruction.destination.spirIndex] + j] = false;
			}
		}
	}

	for (auto it = assigned.begin(); it != assigned.end(); ++it) {
		if (names.find(it->first) != names.end()) {
			if (it->second.type == Temporary)
				it->second.number = currentlyUsed[it->second.spirIndex];
		}
	}

	//Optimize

	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);

	out << "{\n";

	out << "\t\"varnames\": {\n";
	bool first = true;
	for (auto it = assigned.begin(); it != assigned.end(); ++it) {
		if (names.find(it->first) != names.end()) {
			if (!first) {
				out << ",\n";
			}
			first = false;
			Name name = names[it->first];
			out << "\t\t\"" << name.name << "\": \"";
			outputRegister(out, it->second, stage, 0);
			out << "\"";
		}
	}
	out << "\n\t},\n";

	out << "\t\"consts\": {\n";
	int counter = 0;
	for (unsigned i = 0; i < constants.size(); ++i) {
		if (!constants[i].hardCoded) {
			break;
		}
		for (int j = 0; j < constants[i].size; ++j) { //fill all the registers to avoid overlap			
			if (stage == StageVertex) {
				out << "\t\t\"vc" << counter << "\": [";
			}
			else {
				out << "\t\t\"fc" << counter << "\": [";
			}

			for (unsigned j = 0; j < constants[i].operands.size(); ++j) {
				if (j != 0) out << ", ";
				out << constants[i].operands[j];
			}
			out << "]";

			if (i < constants.size() - 1 && constants[i+1].hardCoded) out << ",";
			out << "\n";
			++counter;
		}

	}
	out << "\t},\n";

	out << "\t\"agalasm\": \"";
	for (unsigned i = 0; i < agal.size(); ++i) {
		Agal instruction = agal[i];
		if (instruction.opcode == unknown) {
			out << "Unknown instruction " << instruction.destination.spirIndex << "\\n";
			continue;
		}
		for (int i2 = 0; i2 < instruction.destination.size; ++i2) {
			if (instruction.opcode == con) continue;
			switch (instruction.opcode) {
			case mov:
				out << "mov";
				break;
			case add:
				out << "add";
				break;
			case sub:
				out << "sub";
				break;
			case mul:
				out << "mul";
				break;
			case Opcode::div:
				out << "div";
				break;
			case m44:
				out << "m44";
				break;
			case tex:
				out << "tex";
				break;
			case Opcode::cos:
				out << "cos";
				break;
			case Opcode::sin:
				out << "sin";
				break;
			case nrm:
				out << "nrm";
				break;
			case min:
				out << "min";
				break;
			case max:
				out << "max";
				break;
			case unknown:
				out << "unknown";
				break;
			}
			out << " ";
			outputRegister(out, instruction.destination, stage, i2);
			out << ", ";
			outputRegister(out, instruction.source1, stage, i2);
			if (instruction.source2.type != Unused) {
				out << ", ";
				outputRegister(out, instruction.source2, stage, i2);
				if (instruction.source2.type == Sampler) {
					out << " <2d, wrap, linear>";
				}
			}
			out << "\\n";
		}
	}
	out << "\"\n";

	out << "}\n";

	out.close();
}
