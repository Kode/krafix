#include "GlslTranslator.h"
#include <fstream>
#include <map>
#include <sstream>

using namespace krafix;

typedef unsigned id;

void GlslTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	out.open(filename, std::ios::binary | std::ios::out);

	out << "#version " << target.version << "\n";
	if (target.es) out << "precision mediump float;\n";
	
	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		outputInstruction(target, attributes, inst);
		if (outputting) out << "\n";
	}

	out.close();
}

void GlslTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

	switch (inst.opcode) {
	case OpExecutionMode: {
		output(out);
		switch (inst.operands[1]) {
		case ExecutionModeOutputVertices:
			out << "layout(vertices = " << inst.operands[2] << ") out;";
			break;
		default:
			out << "// Unknown execution mode";
		}
		break;
	}
	case OpTypeArray: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "?[]";
		Type subtype = types[inst.operands[1]];
		if (subtype.name != NULL) {
			if (strcmp(subtype.name, "float") == 0) {
				t.name = "float[]";
				t.length = 2;
				types[id] = t;
			}
			if (strcmp(subtype.name, "vec3") == 0) {
				t.name = "vec3[]";
				t.length = 2;
				types[id] = t;
			}
		}
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
	case OpVariable: {
		unsigned id = inst.operands[1];
		Variable& v = variables[id];
		v.type = inst.operands[0];
		v.storage = (StorageClass)inst.operands[2];
		v.declared = v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
		if (names.find(id) != names.end()) {
			references[id] = names[id].name;
		}
		break;
	}
	case OpFunction:
		output(out);
		for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
			unsigned id = v->first;
			Variable& variable = v->second;

			Type t = types[variable.type];
			Name n = names[id];

			if (variable.builtin) {
				if (target.version >= 300 && n.name == "gl_FragColor") {
					n.name = "krafix_FragColor";
					names[id] = n;
				}
				else {
					continue;
				}
			}

			switch (stage) {
			case EShLangVertex:
				if (variable.storage == StorageClassInput) {
					if (target.version < 300) {
						out << "attribute " << t.name << " " << n.name << ";\n";
					}
					else {
						out << "in " << t.name << " " << n.name << ";\n";
					}
				}
				else if (variable.storage == StorageClassOutput) {
					if (target.version < 300) {
						out << "varying " << t.name << " " << n.name << ";\n";
					}
					else {
						out << "out " << t.name << " " << n.name << ";\n";
					}
				}
				else if (variable.storage == StorageClassUniformConstant) {
					out << "uniform " << t.name << " " << n.name << ";\n";
				}
				break;
			case EShLangFragment:
				if (variable.storage == StorageClassInput) {
					if (target.version < 300) {
						out << "varying " << t.name << " " << n.name << ";\n";
					}
					else {
						out << "in " << t.name << " " << n.name << ";\n";
					}
				}
				else if (variable.storage == StorageClassUniformConstant) {
					out << "uniform " << t.name << " " << n.name << ";\n";
				}
				break;
			case EShLangGeometry:
			case EShLangTessControl:
			case EShLangTessEvaluation:
				if (variable.storage == StorageClassInput) {
					out << "in " << t.name << " " << n.name << ";\n";
				}
				else if (variable.storage == StorageClassOutput) {
					out << "out " << t.name << " " << n.name << ";\n";
				}
				else if (variable.storage == StorageClassUniformConstant) {
					out << "uniform " << t.name << " " << n.name << ";\n";
				}
				break;
			}
		}
		out << "\n";
		indent(out);
		if (target.kore) out << "void kore()\n";
		else out << "void main()\n";
		indent(out);
		out << "{";
		++indentation;
		break;
	case OpCompositeConstruct: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		std::stringstream str;
		str << "vec4(" << getReference(inst.operands[2]) << ", " << getReference(inst.operands[3]) << ", "
			<< getReference(inst.operands[4]) << ", " << getReference(inst.operands[5]) << ")";
		references[result] = str.str();
		break;
	}
	case OpTextureSample: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id sampler = inst.operands[2];
		id coordinate = inst.operands[3];
		std::stringstream str;
		if (target.version < 300) str << "texture2D";
		else str << "texture";
		str << "(" << getReference(sampler) << ", " << getReference(coordinate) << ")";
		references[result] = str.str();
		break;
	}
	case OpReturn:
		output(out);
		out << "return;";
		break;
	case OpStore: {
		output(out);
		Variable v = variables[inst.operands[0]];
		if (stage == EShLangFragment && v.storage == StorageClassOutput && target.version < 300) {
			out << "gl_FragColor" << " = " << getReference(inst.operands[1]) << ";";
		}
		else if (!v.declared) {
			out << types[v.type].name << " " << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
			v.declared = true;
		}
		else {
			out << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
		}
		break;
	}
	default:
		CStyleTranslator::outputInstruction(target, attributes, inst);
		break;
	}
}
