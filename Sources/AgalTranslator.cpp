#include "AgalTranslator.h"
#include <fstream>
#include <map>
#include <string.h>

using namespace krafix;

namespace {
	struct Variable {
		unsigned type;
		spv::StorageClass storage;
		bool builtin;

		Variable() : builtin(false) {}
	};

	std::map<unsigned, Variable> variables;

	enum Opcode {
		add,
		mov,
		m44,
		unknown
	};

	enum RegisterType {
		Attribute,
		Constant,
		Temporary,
		VertexOutput,
		FragmentOutput,
		Varying,
		Sampler,
		Unused
	};

	struct Register {
		RegisterType type;
		int number;
		unsigned spirIndex;

		Register() : type(Unused), number(-1), spirIndex(0) { }

		Register(unsigned spirIndex) : number(-1), spirIndex(spirIndex) {
			if (variables.find(spirIndex) == variables.end()) {
				type = Temporary;
			}
			else {
				Variable variable = variables[spirIndex];
				switch (variable.storage) {
				case spv::StorageClassUniformConstant:
					type = Constant;
					break;
				case spv::StorageClassInput:
					type = Attribute;
					break;
				case spv::StorageClassUniform:
					type = Constant;
					break;
				case spv::StorageClassOutput:
					type = Varying;
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

	struct Type {
		const char* name;
		unsigned length;

		Type() : name("unknown"), length(1) {}
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

	void assignRegisterNumber(Register& reg, std::map<unsigned, int>& assigned, int& nextTemporary, int& nextAttribute, int& nextVarying, int& nextConstant) {
		if (reg.type == Unused) return;

		if (assigned.find(reg.spirIndex) != assigned.end()) {
			reg.number = assigned[reg.spirIndex];
		}
		else {
			switch (reg.type) {
			case Temporary:
				reg.number = nextTemporary;
				assigned[reg.spirIndex] = nextTemporary;
				++nextTemporary;
				break;
			case Attribute:
				reg.number = nextAttribute;
				assigned[reg.spirIndex] = nextAttribute;
				++nextAttribute;
				break;
			case Varying:
				reg.number = nextVarying;
				assigned[reg.spirIndex] = nextVarying;
				++nextVarying;
				break;
			case Constant:
				reg.number = nextConstant;
				assigned[reg.spirIndex] = nextConstant;
				++nextConstant;
				break;
			}
		}
	}

	void assignRegisterNumbers(std::vector<Agal>& agal) {
		std::map<unsigned, int> assigned;
		int nextTemporary = 0;
		int nextAttribute = 0;
		int nextVarying = 0;
		int nextConstant = 0;

		for (unsigned i = 0; i < agal.size(); ++i) {
			Agal& instruction = agal[i];
			assignRegisterNumber(instruction.destination, assigned, nextTemporary, nextAttribute, nextVarying, nextConstant);
			assignRegisterNumber(instruction.source1, assigned, nextTemporary, nextAttribute, nextVarying, nextConstant);
			assignRegisterNumber(instruction.source2, assigned, nextTemporary, nextAttribute, nextVarying, nextConstant);
		}
	}

	void outputRegister(std::ostream& out, Register reg) {
		switch (reg.type) {
		case Attribute:
			out << "va";
			break;
		case Constant:
			out << "vc";
			break;
		case Temporary:
			out << "vt";
			break;
		case VertexOutput:
			out << "op";
			break;
		case FragmentOutput:
			out << "oc";
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
		if (reg.type != VertexOutput && reg.type != FragmentOutput) {
			out << reg.number;
		}
	}
}

void AgalTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Type> types;
	variables.clear();

	std::vector<Agal> agal;

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		case OpName: {
			Name n;
			unsigned id = inst.operands[0];
			n.name = inst.string;
			names[id] = n;
			break;
		}
		case OpTypePointer: {
			Type t;
			unsigned id = inst.operands[0];
			Type subtype = types[inst.operands[2]];
			t.name = subtype.name;
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
		case OpTypeVector: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "vec?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 2) {
					t.name = "vec2";
					t.length = 2;
					types[id] = t;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
					t.name = "vec3";
					t.length = 3;
					types[id] = t;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
					t.name = "vec4";
					t.length = 4;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeMatrix: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "mat?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "vec4") == 0 && inst.operands[2] == 4) {
					t.name = "mat4";
					t.length = 4;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeSampler: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "sampler2D";
			types[id] = t;
			break;
		}
		case OpConstant: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			//out << "const " << resultType.name << " _" << result << " = " << *(float*)&inst.operands[2] << ";\n";
			break;
		}
		case OpVariable: {
			unsigned id = inst.operands[1];
			Variable& v = variables[id];
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
			//out << "\t" << resultType.name << " _" << result << " = vec4(_"
			//	<< inst.operands[2] << ", _" << inst.operands[3] << ", _"
			//	<< inst.operands[4] << ", _" << inst.operands[5] << ");\n";
			break;
		}
		case OpCompositeExtract: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned composite = inst.operands[2];
			//out << "\t" << resultType.name << " _" << result << " = _"
			//	<< composite << "." << indexName(inst.operands[3]) << ";\n";
			break;
		}
		case OpMatrixTimesVector: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned matrix = inst.operands[2];
			unsigned vector = inst.operands[3];
			agal.push_back(Agal(m44, Register(result), Register(matrix), Register(vector)));
			break;
		}
		case OpImageSampleImplicitLod: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned sampler = inst.operands[2];
			unsigned coordinate = inst.operands[3];
			//out << "\t" << resultType.name << " _" << result << " = texture2D(_" << sampler << ", _" << coordinate << ");\n";
			break;
		}
		case OpVectorShuffle: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector1 = inst.operands[2];
			unsigned vector1length = 4; // types[variables[inst.operands[2]].type].length;
			unsigned vector2 = inst.operands[3];
			unsigned vector2length = 4; // types[variables[inst.operands[3]].type].length;

			//out << "\t" << resultType.name << " _" << result << " = " << resultType.name << "(";
			//for (unsigned i = 4; i < inst.length; ++i) {
			//	unsigned index = inst.operands[i];
			//	if (index < vector1length) out << "_" << vector1 << "." << indexName(index);
			//	else out << "_" << vector2 << "." << indexName(index - vector1length);
			//	if (i < inst.length - 1) out << ", ";
			//}
			//out << ");\n";*/
			break;
		}
		case OpFMul: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			//out << "\t" << resultType.name << " _" << result << " = _"
			//	<< operand1 << " * _" << operand2 << ";\n";
			break;
		}
		case OpVectorTimesScalar: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector = inst.operands[2];
			unsigned scalar = inst.operands[3];
			//out << "\t" << resultType.name << " _" << result << " = _"
			//	<< vector << " * _" << scalar << ";\n";
			break;
		}
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
			agal.push_back(Agal(mov, Register(inst.operands[1]), Register(inst.operands[2])));
			break;
		}
		case OpStore: {
			Variable v = variables[inst.operands[0]];
			if (v.builtin && stage == EShLangFragment) {
				Register oc;
				oc.type = FragmentOutput;
				oc.number = 0;
				agal.push_back(Agal(mov, oc, Register(inst.operands[1])));
			}
			else {
				agal.push_back(Agal(mov, Register(inst.operands[0]), Register(inst.operands[1])));
			}
			break;
		}
		default:
			Agal instruction(unknown, Register(), Register());
			instruction.destination.number = inst.opcode;
			agal.push_back(instruction);
			break;
		}
	}

	assignRegisterNumbers(agal);

	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);
	for (unsigned i = 0; i < agal.size(); ++i) {
		Agal instruction = agal[i];
		switch (instruction.opcode) {
		case mov:
			out << "mov";
			break;
		case add:
			out << "add";
			break;
		case m44:
			out << "m44";
			break;
		case unknown:
			out << "unknown";
		}
		out << " ";
		outputRegister(out, instruction.destination);
		out << ", ";
		outputRegister(out, instruction.source1);
		if (instruction.source2.type != Unused) {
			out << ", ";
			outputRegister(out, instruction.source2);
		}
		out << "\n";
	}
	out.close();
}
