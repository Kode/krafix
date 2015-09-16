#include "AgalTranslator.h"
#include <fstream>
#include <map>
#include <string.h>
#include <sstream>

using namespace krafix;

namespace {
	typedef unsigned id;

	struct Variable {
		unsigned id;
		unsigned type;
		spv::StorageClass storage;
		bool builtin;
		
		Variable() : builtin(false) {}
	};

	std::map<unsigned, Variable> variables;

	enum Opcode {
		con, // pseudo instruction for constants
		add,
		mul,
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
		int size;
		const char* swizzle;
		unsigned spirIndex;

		Register() : type(Unused), number(-1), swizzle("xyzw"), size(1), spirIndex(0) { }

		Register(unsigned spirIndex, const char* swizzle = "xyzw", int size = 1) : number(-1), swizzle(swizzle), size(size), spirIndex(spirIndex) {
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
		bool isarray;

		Type() : name("unknown"), length(1), isarray(false) {}
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

	void assignRegisterNumber(Register& reg, std::map<unsigned, Register>& assigned, int& nextTemporary, int& nextAttribute, int& nextVarying, int& nextConstant) {
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
				reg.number = nextVarying;
				if (reg.spirIndex != 0) assigned[reg.spirIndex] = reg;
				nextVarying += reg.size;
				break;
			case Constant:
				reg.number = nextConstant;
				if (reg.spirIndex != 0) assigned[reg.spirIndex] = reg;
				nextConstant += reg.size;
				break;
			}
		}
	}

	void assignRegisterNumbers(std::vector<Agal>& agal, std::map<unsigned, Register>& assigned) {
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

	void outputRegister(std::ostream& out, Register reg, int registerOffset) {
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
			out << reg.number + registerOffset;
		}
		if (strcmp(reg.swizzle, "xyzw") != 0) {
			out << "." << reg.swizzle;
		}
	}
}

void AgalTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Type> types;
	std::vector<std::string> constants;
	variables.clear();
	unsigned vertexOutput = 0;

	std::vector<Agal> agal;

	{
		Register reg(99999);
		reg.type = Constant;
		agal.push_back(Agal(con, reg, Register()));
		constants.push_back("0.5");
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

			constants.push_back(value);

			Register reg(result);
			reg.type = Constant;
			agal.push_back(Agal(con, reg, Register()));

			break;
		}
		case OpConstantComposite: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;

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
					t.length = 1;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
					t.name = "vec3";
					t.length = 1;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
					t.name = "vec4";
					t.length = 1;
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
					t.length = 4;
					types[id] = t;
				}
				else if (strcmp(subtype.name, "vec4") == 0 && inst.operands[2] == 4) {
					t.name = "mat4";
					t.length = 4;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeImage: {
			Type t;
			unsigned id = inst.operands[0];
			bool video = inst.length >= 8 && inst.operands[8] == 1;
			if (video && target.system == Android) {
				t.name = "samplerExternalOES";
			}
			else {
				t.name = "sampler2D";
			}
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
			agal.push_back(Agal(mov, Register(result, "x"), Register(inst.operands[2], "x")));
			agal.push_back(Agal(mov, Register(result, "y"), Register(inst.operands[3], "y")));
			agal.push_back(Agal(mov, Register(result, "z"), Register(inst.operands[4], "z")));
			agal.push_back(Agal(mov, Register(result, "w"), Register(inst.operands[5], "w")));
			break;
		}
		case OpCompositeExtract: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned composite = inst.operands[2];
			agal.push_back(Agal(mov, Register(result, "xyzw"), Register(composite, indexName4(inst.operands[3]))));
			break;
		}
		case OpMatrixTimesVector: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned matrix = inst.operands[2];
			unsigned vector = inst.operands[3];
			agal.push_back(Agal(m44, Register(result), Register(vector), Register(matrix, "xyzw", 4)));
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
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;

			Register r1(result);
			r1.size = resultType.length;
			Register r2(inst.operands[2]);
			r2.size = types[inst.operands[2]].length;

			agal.push_back(Agal(mov, r1, r2));
			break;
		}
		case OpStore: {
			Variable v = variables[inst.operands[0]];
			if (v.builtin && stage == EShLangFragment) {
				Register oc(inst.operands[0]);
				oc.type = FragmentOutput;
				oc.number = 0;
				agal.push_back(Agal(mov, oc, Register(inst.operands[1])));
			}
			else if (v.builtin && stage == EShLangVertex) {
				vertexOutput = inst.operands[0];
				Register tempop(inst.operands[0]);
				tempop.type = Temporary;
				agal.push_back(Agal(mov, tempop, Register(inst.operands[1])));
			}
			else {
				Type t1 = types[inst.operands[0]];
				Type t2 = types[inst.operands[1]];
				Register r1(inst.operands[0]);
				if (strcmp(t1.name, "mat4") == 0) {
					r1.size = 4;
				}
				Register r2(inst.operands[1]);
				if (strcmp(t2.name, "mat4") == 0) {
					r2.size = 4;
				}
				agal.push_back(Agal(mov, r1, r2));
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

	//adjust clip space
	{
		Register poszzzz(vertexOutput, "zzzz");
		Register poswwww(vertexOutput, "wwww");
		agal.push_back(Agal(add, Register(99998, "xxxx"), poszzzz, poswwww));
		Register posz(vertexOutput, "z");
		Register reg(99999);
		reg.type = Constant;
		reg.swizzle = "x";
		agal.push_back(Agal(mul, posz, reg, Register(99998, "x")));
		
		Register op(0);
		op.type = VertexOutput;
		op.number = 0;
		agal.push_back(Agal(mov, op, Register(vertexOutput)));
	}

	std::map<unsigned, Register> assigned;
	assignRegisterNumbers(agal, assigned);

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
			outputRegister(out, it->second, 0);
			out << "\"";
		}
	}
	out << "\n\t},\n";
	
	out << "\t\"consts\": {\n";
	for (unsigned i = 0; i < constants.size(); ++i) {
		out << "\t\t\"vc" << i << "\": [" << constants[i] << ", " << constants[i] << ", " << constants[i] << ", " << constants[i] << "]";
		if (i < constants.size() - 1) out << ",";
		out << "\n";
	}
	out << "\t},\n";
	
	out << "\t\"agalasm\": \"";
	for (unsigned i = 0; i < agal.size(); ++i) {
		Agal instruction = agal[i];
		for (int i2 = 0; i2 < instruction.destination.size; ++i2) {
			if (instruction.opcode == con) continue;
			switch (instruction.opcode) {
			case mov:
				out << "mov";
				break;
			case add:
				out << "add";
				break;
			case mul:
				out << "mul";
				break;
			case m44:
				out << "m44";
				break;
			case unknown:
				out << "unknown";
				break;
			}
			out << " ";
			outputRegister(out, instruction.destination, i2);
			out << ", ";
			outputRegister(out, instruction.source1, i2);
			if (instruction.source2.type != Unused) {
				out << ", ";
				outputRegister(out, instruction.source2, i2);
			}
			out << "\\n";
		}
	}
	out << "\"\n";

	out << "}\n";

	out.close();
}
