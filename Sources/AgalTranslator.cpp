#include "AgalTranslator.h"
#include <fstream>
#include <map>

using namespace krafix;

namespace {
	struct Variable {
		unsigned type;
		spv::StorageClass storage;
		bool builtin;

		Variable() : builtin(false) {}
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
}

void AgalTranslator::outputCode(const char* baseName) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Type> types;
	std::map<unsigned, Variable> variables;

	std::ofstream out;
	std::string fileName(baseName);
	fileName.append(".agal");
	out.open(fileName.c_str(), std::ios::binary | std::ios::out);

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
			out << "const " << resultType.name << " _" << result << " = " << *(float*)&inst.operands[2] << ";\n";
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
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;

				if (variable.builtin) continue;

				Type t = types[variable.type];
				Name n = names[id];

				switch (stage) {
				case EShLangVertex:
					if (variable.storage == StorageClassInput) {
						out << "attribute " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassOutput) {
						out << "varying " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassUniformConstant) {
						out << "uniform " << t.name << " " << n.name << ";\n";
					}
					break;
				case EShLangFragment:
					if (variable.storage == StorageClassInput) {
						out << "varying " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassUniformConstant) {
						out << "uniform " << t.name << " " << n.name << ";\n";
					}
					break;
				}
			}
			break;
		case OpFunctionEnd:
			break;
		case OpCompositeConstruct: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			out << "\t" << resultType.name << " _" << result << " = vec4(_"
				<< inst.operands[2] << ", _" << inst.operands[3] << ", _"
				<< inst.operands[4] << ", _" << inst.operands[5] << ");\n";
			break;
		}
		case OpCompositeExtract: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned composite = inst.operands[2];
			out << "\t" << resultType.name << " _" << result << " = _"
				<< composite << "." << indexName(inst.operands[3]) << ";\n";
			break;
		}
		case OpMatrixTimesVector: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned matrix = inst.operands[2];
			unsigned vector = inst.operands[3];
			out << "\t" << resultType.name << " _" << result << " = _" << matrix << " * _" << vector << ";\n";
			break;
		}
		case OpTextureSample: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned sampler = inst.operands[2];
			unsigned coordinate = inst.operands[3];
			out << "\t" << resultType.name << " _" << result << " = texture2D(_" << sampler << ", _" << coordinate << ");\n";
			break;
		}
		case OpVectorShuffle: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector1 = inst.operands[2];
			unsigned vector1length = 4; // types[variables[inst.operands[2]].type].length;
			unsigned vector2 = inst.operands[3];
			unsigned vector2length = 4; // types[variables[inst.operands[3]].type].length;

			out << "\t" << resultType.name << " _" << result << " = " << resultType.name << "(";
			for (unsigned i = 4; i < inst.length; ++i) {
				unsigned index = inst.operands[i];
				if (index < vector1length) out << "_" << vector1 << "." << indexName(index);
				else out << "_" << vector2 << "." << indexName(index - vector1length);
				if (i < inst.length - 1) out << ", ";
			}
			out << ");\n";
			break;
		}
		case OpFMul: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			out << "\t" << resultType.name << " _" << result << " = _"
				<< operand1 << " * _" << operand2 << ";\n";
			break;
		}
		case OpVectorTimesScalar: {
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector = inst.operands[2];
			unsigned scalar = inst.operands[3];
			out << "\t" << resultType.name << " _" << result << " = _"
				<< vector << " * _" << scalar << ";\n";
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
		case OpLoad: {
			Type t = types[inst.operands[0]];
			Variable& v = variables[inst.operands[2]];
			if (names.find(inst.operands[2]) != names.end()) {
				Name n = names[inst.operands[2]];
				out << "mov " << inst.operands[1] << ", " << inst.operands[2] << "\n";
			}
			else {
				out << "mov " << inst.operands[1] << ", " << inst.operands[2] << "\n";
			}
			break;
		}
		case OpStore: {
			Variable v = variables[inst.operands[0]];
			if (v.builtin && stage == EShLangFragment) {
				out << "mov oc, " << inst.operands[1] << "\n";
			}
			else {
				out << "mov " << inst.operands[0] << ", " << inst.operands[1] << "\n";
			}
			break;
		}
		default:
			out << "Unknown operation " << inst.opcode << ".\n";
			break;
		}
	}

	out.close();
}
