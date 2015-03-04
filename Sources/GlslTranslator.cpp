#include "GlslTranslator.h"
#include <fstream>
#include <map>

using namespace krafix;

namespace {
	struct Variable {
		unsigned type;
		spv::StorageClass storage;
	};

	struct Type {
		const char* name;
	};

	struct Name {
		const char* name;
	};
}

void GlslTranslator::outputCode(const char* baseName) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Type> types;
	std::map<unsigned, Variable> variables;

	std::ofstream out;
	std::string fileName(baseName);
	fileName.append(".glsl");
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
		case OpTypeVector: {
			Type t;
			unsigned id = inst.operands[0];
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
					t.name = "vec4";
					types[id] = t;
				}
			}
			break;
		}
		case OpVariable: {
			Variable v;
			unsigned id = inst.operands[1]; 
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];
			variables[id] = v;

			Type t = types[v.type];
			Name n = names[id];

			if (v.storage == StorageInput) {
				out << "varying " << t.name << " " << n.name << ";\n";
			}

			break;
		}
		case OpFunction:
			out << "\nvoid main() {\n";
			break;
		case OpFunctionEnd:
			out << "}\n";
			break;
		case OpLoad: {
			Type t = types[inst.operands[0]];
			if (names.find(inst.operands[2]) != names.end()) {
				Name n = names[inst.operands[2]];
				out << "\t" << t.name << " _" << inst.operands[1] << " = " << n.name << ";\n";
			}
			else {
				out << "\t" << t.name << " _" << inst.operands[1] << " = _" << inst.operands[2] << ";\n";
			}
			break;
		}
		case OpStore: {
			Variable v = variables[inst.operands[0]];
			if (v.storage == StorageOutput) {
				out << "\tgl_FragColor" << " = _" << inst.operands[1] << ";\n";
			}
			else {
				out << "\t*" << inst.operands[0] << " = _" << inst.operands[1] << ";\n";
			}
			break;
		}
		}
	}

	out.close();
}
