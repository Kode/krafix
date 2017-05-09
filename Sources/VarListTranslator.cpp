#include "VarListTranslator.h"
#include <SPIRV/spirv.hpp>
#include "../glslang/glslang/Public/ShaderLang.h"
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
		char name[256];
		unsigned length;
		bool isarray;

		Type() : length(1), isarray(false) {
			strcpy(name, "unknown");
		}
	};

	struct Variable {
		unsigned id;
		unsigned type;
		spv::StorageClass storage;
		bool builtin;

		Variable() : builtin(false) {}
	};

	void namesAndTypes(Instruction& inst, std::map<unsigned, Name>& names, std::map<unsigned, Type>& types) {
		using namespace spv;

		switch (inst.opcode) {
		case OpTypePointer: {
			Type t;
			unsigned id = inst.operands[0];
			Type subtype = types[inst.operands[2]];
			strcpy(t.name, subtype.name);
			t.isarray = subtype.isarray;
			t.length = subtype.length;
			types[id] = t;
			break;
		}
		case OpTypeFloat: {
			Type t;
			unsigned id = inst.operands[0];
			strcpy(t.name, "float");
			types[id] = t;
			break;
		}
		case OpTypeInt: {
			Type t;
			unsigned id = inst.operands[0];
			strcpy(t.name, "int");
			types[id] = t;
			break;
		}
		case OpTypeBool: {
			Type t;
			unsigned id = inst.operands[0];
			strcpy(t.name, "bool");
			types[id] = t;
			break;
		}
		case OpTypeStruct: {
			Type t;
			unsigned id = inst.operands[0];
			// TODO: members
			Name n = names[id];
			strcpy(t.name, n.name);
			types[id] = t;
			break;
		}
		case OpTypeArray: {
			Type t;
			strcpy(t.name, "[]");
			t.isarray = true;
			unsigned id = inst.operands[0];
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				strcpy(t.name, subtype.name);
				strcat(t.name, "[]");
			}
			types[id] = t;
			break;
		}
		case OpTypeVector: {
			Type t;
			unsigned id = inst.operands[0];
			strcpy(t.name, "vec?");
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 2) {
					strcpy(t.name, "vec2");
					t.length = 2;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
					strcpy(t.name, "vec3");
					t.length = 3;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
					strcpy(t.name, "vec4");
					t.length = 4;
				}
			}
			types[id] = t;
			break;
		}
		case OpTypeMatrix: {
			Type t;
			unsigned id = inst.operands[0];
			strcpy(t.name, "mat?");
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "vec3") == 0 && inst.operands[2] == 3) {
					strcpy(t.name, "mat3");
					t.length = 4;
					types[id] = t;
				}
				else if (strcmp(subtype.name, "vec4") == 0 && inst.operands[2] == 4) {
					strcpy(t.name, "mat4");
					t.length = 4;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeImage: {
			Type t;
			unsigned id = inst.operands[0];
			int dim = inst.operands[2] + 1;
			bool depth = inst.operands[3] != 0;
			bool arrayed = inst.operands[4] != 0;
			bool video = inst.length >= 8 && inst.operands[8] == 1;
			if (video) {
				strcpy(t.name, "samplerVideo");
			}
			else {
				char name[128];
				strcpy(name, "sampler");
				if (dim == 4) {
					strcat(name, "Cube");
				}
				else {
					size_t length = strlen(name);
					sprintf(&name[length], "%d", dim);
					name[length + 1] = 0;
					strcat(name, "D");
				}
				if (depth) {
					strcat(name, "Shadow");
				}
				if (arrayed) {
					strcat(name, "Array");
				}
				strcpy(t.name, name);
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
		}
	}
}

void VarListTranslator::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Variable> variables;
	std::map<unsigned, Type> types;

	std::streambuf* buf;
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
	case StageVertex:
		out << "vertex\n";
		break;
	case StageFragment:
		out << "fragment\n";
		break;
	case StageGeometry:
		out << "geometry\n";
		break;
	case StageTessControl:
		out << "tesscontrol\n";
		break;
	case StageTessEvaluation:
		out << "tessevaluation\n";
		break;
	case StageCompute:
		out << "compute\n";
		break;
	}

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		default:
			namesAndTypes(inst, names, types);
			break;
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

void VarListTranslator::print() {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Variable> variables;
	std::map<unsigned, Type> types;

	switch (stage) {
	case StageVertex:
		std::cerr << "#shader:vertex" << std::endl;
		break;
	case StageFragment:
		std::cerr << "#shader:fragment" << std::endl;
		break;
	case StageGeometry:
		std::cerr << "#shader:geometry" << std::endl;
		break;
	case StageTessControl:
		std::cerr << "#shader:tesscontrol" << std::endl;
		break;
	case StageTessEvaluation:
		std::cerr << "#shader:tessevaluation" << std::endl;
		break;
	case StageCompute:
		std::cerr << "#shader:compute" << std::endl;
		break;
	}

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		default:
			namesAndTypes(inst, names, types);
			break;
		case OpVariable: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;
			Variable& v = variables[result];
			v.id = result;
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];

			if (names.find(result) != names.end()) {
				std::string storage;
				if (v.storage == StorageClassUniformConstant) {
					storage = "uniform";
				}
				else if (v.storage == StorageClassInput) {
					storage = "input";
				}
				else if (v.storage == StorageClassOutput) {
					storage = "output";
				}
				else {
					break;
				}
				std::cerr << "#" << storage << ":" << names[result].name << ":" << types[result].name << std::endl;
			}

			break;
		}
		}
	}
}
