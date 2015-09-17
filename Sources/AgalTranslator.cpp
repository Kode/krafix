#include "AgalTranslator.h"
#include <algorithm>
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

	struct Type {
		const char* name;
		unsigned length;
		bool isarray;

		Type() : name("unknown"), length(1), isarray(false) {}
	};

	std::map<unsigned, Variable> variables;
	std::map<unsigned, Type> types;

	enum Opcode {
		con, // pseudo instruction for constants
		add,
		mul,
		mov,
		m44,
		tex,
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
		const char* swizzle;
		unsigned spirIndex;

		Register() : type(Unused), number(-1), swizzle("xyzw"), size(1), spirIndex(0) { }

		Register(EShLanguage stage, unsigned spirIndex, const char* swizzle = "xyzw", int size = 1) : number(-1), swizzle(swizzle), size(size), spirIndex(spirIndex) {
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
					}
					break;
				}
				case spv::StorageClassInput:
					if (stage == EShLangVertex) type = Attribute;
					else type = Varying;
					break;
				case spv::StorageClassUniform:
					type = Constant;
					break;
				case spv::StorageClassOutput:
					if (stage == EShLangVertex) type = Varying;
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
				reg.number = nextSampler;
				if (reg.spirIndex != 0) assigned[reg.spirIndex] = reg;
				nextSampler += reg.size;
				break;
			}
		}
	}

	bool includes(const std::vector<std::string>& array, const std::string& value) {
		for (auto s : array) {
			if (s == value) return true;
		}
		return false;
	}

	void assignRegisterNumbers(std::vector<Agal>& agal, std::map<unsigned, Register>& assigned, std::map<unsigned, Name>& names) {
		int nextTemporary = 0;
		int nextAttribute = 0;
		int nextConstant = 0;
		int nextSampler = 0;

		std::vector<std::string> varyings;
		for (unsigned i = 0; i < agal.size(); ++i) {
			Agal& instruction = agal[i];
			if (instruction.destination.type == Varying) {
				std::string name = names[instruction.destination.spirIndex].name;
				if (!includes(varyings, name)) varyings.push_back(name);
			}
			else if (instruction.source1.type == Varying) {
				std::string name = names[instruction.source1.spirIndex].name;
				if (!includes(varyings, name)) varyings.push_back(name);
			}
			else if (instruction.source2.type == Varying) {
				std::string name = names[instruction.source2.spirIndex].name;
				if (!includes(varyings, name)) varyings.push_back(name);
			}
		}
		std::sort(varyings.begin(), varyings.end());
		std::map<std::string, int> varyingNumbers;
		for (unsigned i = 0; i < varyings.size(); ++i) {
			varyingNumbers[varyings[i]] = i;
		}

		for (unsigned i = 0; i < agal.size(); ++i) {
			Agal& instruction = agal[i];
			if (instruction.destination.type == Varying) {
				std::string name = names[instruction.destination.spirIndex].name;
				instruction.destination.number = varyingNumbers[name];
				assigned[instruction.destination.spirIndex] = instruction.destination;
			}
			else if (instruction.source1.type == Varying) {
				std::string name = names[instruction.source1.spirIndex].name;
				instruction.source1.number = varyingNumbers[name];
				assigned[instruction.source1.spirIndex] = instruction.source1;
			}
			else if (instruction.source2.type == Varying) {
				std::string name = names[instruction.source2.spirIndex].name;
				instruction.source2.number = varyingNumbers[name];
				assigned[instruction.source2.spirIndex] = instruction.source2;
			}
		}

		for (unsigned i = 0; i < agal.size(); ++i) {
			Agal& instruction = agal[i];
			assignRegisterNumber(instruction.destination, assigned, nextTemporary, nextAttribute, nextConstant, nextSampler);
			assignRegisterNumber(instruction.source1, assigned, nextTemporary, nextAttribute, nextConstant, nextSampler);
			assignRegisterNumber(instruction.source2, assigned, nextTemporary, nextAttribute, nextConstant, nextSampler);
		}
	}

	void outputRegister(std::ostream& out, Register reg, EShLanguage stage, int registerOffset) {
		switch (reg.type) {
		case Attribute:
			out << "va";
			break;
		case Constant:
			if (stage == EShLangVertex) out << "vc";
			else out << "fc";
			break;
		case Temporary:
			if (stage == EShLangVertex) out << "vt";
			else out << "ft";
			break;
		case Output:
			if (stage == EShLangVertex) out << "op";
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
		if (strcmp(reg.swizzle, "xyzw") != 0) {
			out << "." << reg.swizzle;
		}
	}
}

void AgalTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::vector<std::string> constants;
	types.clear();
	variables.clear();
	unsigned vertexOutput = 0;

	std::vector<Agal> agal;

	if (stage == EShLangVertex)
	{
		Register reg(stage, 99999);
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

			Register reg(stage, result);
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

			Register r1(stage, result);
			r1.size = resultType.length;
			Register r2(stage, inst.operands[2]);
			r2.size = types[inst.operands[2]].length;

			if (strcmp(types[inst.operands[2]].name, "sampler2D") == 0) {

			}
			else {
				agal.push_back(Agal(mov, r1, r2));
			}
			break;
		}
		case OpStore: {
			Variable v = variables[inst.operands[0]];
			if (v.builtin && stage == EShLangFragment) {
				Register oc(stage, inst.operands[0]);
				oc.type = Output;
				oc.number = 0;
				agal.push_back(Agal(mov, oc, Register(stage, inst.operands[1])));
			}
			else if (v.builtin && stage == EShLangVertex) {
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
		default:
			Agal instruction(unknown, Register(), Register());
			instruction.destination.number = inst.opcode;
			agal.push_back(instruction);
			break;
		}
	}

	//adjust clip space
	if (stage == EShLangVertex)
	{
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
	assignRegisterNumbers(agal, assigned, names);

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
			case tex:
				out << "tex";
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
