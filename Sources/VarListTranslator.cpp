#include "VarListTranslator.h"
#include <fstream>
#include <string.h>
#include <iostream>

using namespace krafix;

namespace {
	typedef unsigned id;

	struct Name {
		const char* name;
	};

	struct Type {
		const char* name;
		unsigned length;
		bool isarray;

		Type() : name("unknown"), length(1), isarray(false) {}
	};

	struct Variable {
		unsigned id;
		unsigned type;
		spv::StorageClass storage;
		bool builtin;

		Variable() : builtin(false) {}
	};
}

void VarListTranslator::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Variable> variables;
	std::map<unsigned, Type> types;

	std::streambuf * buf;
	std::ofstream of;

	if (strcmp(filename, "--") != 0) {
		of.open(filename, std::ios::binary | std::ios::out);
		buf = of.rdbuf();
	}
	else {
		buf = std::cout.rdbuf();
	}

	std::ostream out(buf);

	switch (stage) {
	case EShLangVertex:
		out << "vertex\n";
		break;
	case EShLangFragment:
		out << "fragment\n";
		break;
	case EShLangGeometry:
		out << "geometry\n";
		break;
	case EShLangTessControl:
		out << "tesscontrol\n";
		break;
	case EShLangTessEvaluation:
		out << "tessevaluation\n";
		break;
	case EShLangCompute:
		out << "compute\n";
		break;
	}

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
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
		case OpName: {
			unsigned id = inst.operands[0];
			if (strcmp(inst.string, "") != 0) {
				Name n;
				n.name = inst.string;
				names[id] = n;
			}
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

			if (names.find(result) != names.end()) {
				if (v.storage == StorageClassUniformConstant) {
					out << "uniform";
				}
				else if (v.storage == StorageClassInput) {
					out << "in";
				}
				else if (v.storage == StorageClassOutput) {
					out << "out";
				}
				else {
					break;
				}
				out << " " << types[result].name << " " << names[result].name << "\n";
			}

			break;
		}
		}
	}

	if (strcmp(filename, "--") != 0) {
		of.close();
	}
}
